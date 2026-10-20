#!/usr/bin/env python3
"""Prepare a private installation/recovery set. Camera access is read-only to NOR.

No erase, NOR write, reboot, firmware update or U-Boot environment mutation.
Temporary camera files and the read-only config mount are removed on exit.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import tarfile
import zlib

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
PARTS = [(0, 'uboot', 0, 0x40000), (1, 'dtb', 0x40000, 0x10000),
         (2, 'kernel', 0x50000, 0x1c0000), (3, 'rootfs', 0x210000, 0xf0000),
         (4, 'app', 0x300000, 0x4a0000), (5, 'config', 0x7a0000, 0x60000)]
ORIGINAL_SHA = '2794e0c675cbb56c76e56285160eda580b89f29cd67c41f08f971c121b674e60'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def ssh(host, command, data=None):
    return subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                           '-o', 'StrictHostKeyChecking=yes', 'root@' + host, command],
                          input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


def extract_private(data, tree):
    """Only our fresh camera archive: bounded dirs/files and SDK absolute links."""
    tree.mkdir(mode=0o700)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        members = archive.getmembers()
        assert len(members) < 2000
        total = 0
        for m in members:
            parts = PurePosixPath(m.name).parts
            assert not m.name.startswith('/') and '..' not in parts
            parts = tuple(x for x in parts if x != '.')
            if not parts:
                assert m.isdir()
                continue
            target = tree.joinpath(*parts)
            assert target.parent.is_dir()
            assert not any(p.is_symlink() for p in [target, *target.parents])
            if m.isdir():
                target.mkdir(mode=m.mode & 0o777, exist_ok=False)
            elif m.isfile():
                total += m.size
                assert 0 <= m.size <= 16384 and total <= 262144
                target.write_bytes(archive.extractfile(m).read())
                target.chmod(m.mode & 0o777)
            elif m.issym():
                assert parts[:2] == ('openipc-persist', 'sdk-rtsp')
                assert m.linkname == '/app/runtime/sdk-rtsp/' + '/'.join(parts[2:])
                target.symlink_to(m.linkname)
            else:
                raise ValueError('Unsupported private archive entry')
    return {p.relative_to(tree).as_posix(): p.lstat().st_mode & 0xffff for p in tree.rglob("*")}


def tree_records(tree):
    result = {}
    for p in sorted(tree.rglob('*')):
        s = p.lstat()
        entry = {'mode': s.st_mode & 0xffff}
        if p.is_symlink():
            entry['target'] = os.readlink(p)
        elif p.is_dir():
            entry['directory'] = True
        else:
            assert p.is_file()
            entry.update(bytes=s.st_size, sha256=sha(p.read_bytes()))
        result[p.relative_to(tree).as_posix()] = entry
    return result


def verify_jffs2(image, expected):
    """Verify fresh uncompressed JFFS2 nodes/CRCs and round-trip every entry."""
    crc = lambda b: zlib.crc32(b, 0xffffffff) ^ 0xffffffff
    inodes, dirents, used_blocks = {}, [], set()
    pos = 0
    while pos < len(image):
        if image[pos:pos+4] == b'\xff' * 4:
            pos += 4
            continue
        magic, kind, length, header_crc = struct.unpack_from('<HHII', image, pos)
        assert magic == 0x1985 and length >= 12 and pos + length <= len(image)
        assert pos // 4096 == (pos + length - 1) // 4096
        node = image[pos:pos+length]
        assert crc(node[:8]) == header_crc
        if kind == 0xe002:
            assert len(node) >= 68
            ino, version, mode = struct.unpack_from('<III', node, 12)
            uid, gid = struct.unpack_from('<HH', node, 24)
            size = struct.unpack_from('<I', node, 28)[0]
            offset, compressed, decoded = struct.unpack_from('<III', node, 44)
            assert uid == gid == 0 and node[56] == 0
            assert compressed == decoded and len(node) == 68 + compressed
            assert crc(node[:60]) == struct.unpack_from('<I', node, 64)[0]
            payload = node[68:]
            assert crc(payload) == struct.unpack_from('<I', node, 60)[0]
            entry = inodes.setdefault(ino, {'mode': mode, 'size': size, 'chunks': []})
            assert (entry['mode'], entry['size']) == (mode, size)
            assert offset + decoded <= size
            entry['chunks'].append((version, offset, payload))
        elif kind == 0xe001:
            assert len(node) >= 40
            parent, version, ino = struct.unpack_from('<III', node, 12)
            name = node[40:]
            assert len(name) == node[28] and 0 < len(name) <= 254
            assert crc(node[:32]) == struct.unpack_from('<I', node, 32)[0]
            assert crc(name) == struct.unpack_from('<I', node, 36)[0]
            name = name.decode()
            assert name not in {'.', '..'} and '/' not in name and '\0' not in name and ino
            dirents.append((parent, ino, name))
        else:
            assert kind in {0x2003, 0x2004}
        # Cleanmarkers alone do not consume the remaining erase block.
        if kind != 0x2003:
            used_blocks.add(pos // 4096)
        pos = (pos + length + 3) & ~3
    paths = {1: ''}
    pending = dirents[:]
    while pending:
        before = len(pending)
        for parent, ino, name in pending[:]:
            if parent in paths:
                assert ino not in paths
                paths[ino] = (paths[parent] + '/' + name).lstrip('/')
                pending.remove((parent, ino, name))
        assert len(pending) < before
    actual = {}
    for ino, name in paths.items():
        if not name:
            continue
        e = inodes[ino]
        data = bytearray(e['size'])
        covered = bytearray(e['size'])
        for version, offset, chunk in sorted(e['chunks']):
            assert not any(covered[offset:offset+len(chunk)])
            data[offset:offset+len(chunk)] = chunk
            covered[offset:offset+len(chunk)] = b'\1' * len(chunk)
        assert all(covered)
        record = {'mode': e['mode']}
        if stat.S_ISDIR(e['mode']):
            record['directory'] = True
        elif stat.S_ISLNK(e['mode']):
            record['target'] = bytes(data).decode()
        else:
            assert stat.S_ISREG(e['mode'])
            record.update(bytes=len(data), sha256=sha(data))
        actual[name] = record
    assert actual == expected, 'JFFS2 round-trip differs from the prepared tree'
    return {'all_node_crcs': 'passed', 'all_files_modes_and_links_round_trip': 'passed',
            'entries': len(actual), 'data_eraseblocks': len(used_blocks),
            'eraseblock_bytes': 4096, 'data_block_budget_bytes': len(used_blocks)*4096,
            'remaining_before_kernel_reserve_bytes': 0x60000 - len(used_blocks)*4096,
            'hardware_mount_validated': False}


def pack_4k_nodes(image):
    """mkfs.jffs2 enforces an 8 KiB minimum; target MTD erases 4 KiB.

    JFFS2 nodes contain no physical offsets. Retain every inode/dirent byte,
    placing whole nodes within 4 KiB blocks with a new standard cleanmarker.
    The independent verifier subsequently checks CRCs and the entire tree.
    """
    crc = lambda b: zlib.crc32(b, 0xffffffff) ^ 0xffffffff
    header = struct.pack('<HHI', 0x1985, 0x2003, 12)
    marker = header + struct.pack('<I', crc(header))
    result = bytearray(marker)
    pos = 0
    while pos < len(image):
        if image[pos:pos+4] == b'\xff'*4:
            pos += 4
            continue
        magic, kind, length, checksum = struct.unpack_from('<HHII', image, pos)
        assert magic == 0x1985 and length >= 12 and pos + length <= len(image)
        assert crc(image[pos:pos+8]) == checksum
        node = image[pos:pos+length]
        if kind in {0xe001, 0xe002}:
            assert len(node) <= 4096 - 12
            aligned = (len(node) + 3) & ~3
            if len(result) % 4096 + aligned > 4096:
                result.extend(b'\xff' * (-len(result) % 4096))
                result.extend(marker)
            result.extend(node)
            result.extend(b'\xff' * (-len(result) % 4))
        else:
            assert kind in {0x2003, 0x2004}
        pos = (pos + length + 3) & ~3
    assert len(result) <= 0x60000
    result.extend(b'\xff' * (-len(result) % 4096))
    while len(result) < 0x60000:
        result.extend(marker + b'\xff' * (4096 - len(marker)))
    return bytes(result)


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--camera', default='192.0.2.10')
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9.-]+', args.camera)
    candidate = args.candidate.resolve()
    out = args.output.resolve()
    assert not out.exists(), 'Use a fresh private output directory'
    out.mkdir(parents=True, mode=0o700)
    report = {'nor_written': False, 'rebooted': False, 'installation_authorized': False,
              'hardware_recovery_verified': False, 'installable': False,
              'camera': args.camera, 'candidate': str(candidate.relative_to(BASE))}
    save_json(out/'PREPARATION.json', report)
    original = (BASE/'backup_original_nor.bin').read_bytes()
    assert len(original) == 0x800000 and sha(original) == ORIGINAL_SHA
    # Verify published artifacts before copying; do not alter candidate metadata.
    for row in (candidate/'SHA256SUMS').read_text().splitlines():
        checksum, name = row.split(maxsplit=1)
        assert sha((candidate/name).read_bytes()) == checksum
    snapshot = out/'rollback'
    snapshot.mkdir(mode=0o700)
    first = ssh(args.camera, '/bin/busybox cat /dev/mtd6ro')
    second = ssh(args.camera, '/bin/busybox cat /dev/mtd6ro')
    assert first == second and len(first) == 0x800000, 'Independent NOR reads differ'
    (snapshot/'full-nor-before.bin').write_bytes(first)
    report['rollback'] = {'double_read_identical': True, 'full_nor_sha256': sha(first),
                          'bytes': len(first), 'partitions': []}
    for number, name, start, size in PARTS:
        payload = first[start:start+size]
        (snapshot/f'mtd{number}-{name}.bin').write_bytes(payload)
        report['rollback']['partitions'].append({'partition': f'mtd{number}', 'name': name,
                                                 'offset': start, 'bytes': size, 'sha256': sha(payload)})
    metadata = '''LD=/media/lib/ld-musl-arm.so.1
bb() { "$LD" --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
bb uname -r
bb cat /proc/mtd /proc/cmdline /proc/mounts /proc/uptime
bb grep -E 'MemAvailable|MemFree' /proc/meminfo
(cd /mnt/openipc-persist/sdk-rtsp && bb sha256sum -c SHA256SUMS >/dev/null)
echo SDK_MANIFEST_OK
'''
    (out/'camera-before.txt').write_bytes(ssh(args.camera, '/bin/sh', metadata.encode()))
    inputs = io.BytesIO()
    with tarfile.open(fileobj=inputs, mode='w') as archive:
        archive.add(candidate/'stage-private-config.sh', arcname='stage.sh')
        archive.add(candidate/'SDK-INDEX', arcname='SDK-INDEX')
    remote = '/tmp/anyka-flash-preparation-input'
    musl_bb = '/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox'
    ssh(args.camera, f'({musl_bb} test ! -e {remote}) && {musl_bb} mkdir -m 700 {remote} && {musl_bb} tar -xf - -C {remote}', inputs.getvalue())
    collection = '''set -eu
LD=/media/lib/ld-musl-arm.so.1
bb() { "$LD" --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
work=/tmp/anyka-flash-preparation-input
stage=/tmp/anyka-internal-config-flash-preparation
stock=$work/stock
merged=$work/merged
cleanup() {
    bb umount "$stock" 2>/dev/null || :
    if bb awk -v p="$stock" '$2==p {found=1} END {exit !found}' /proc/mounts; then return 1; fi
    bb rm -rf "$work" "$stage"
}
trap cleanup EXIT HUP INT TERM
[ ! -e "$stage" ]
bb mkdir -m 700 "$stock" "$merged"
bb cat > "$work/bb" <<'WRAPPER'
#!/bin/sh
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
WRAPPER
bb chmod 700 "$work/bb"
BUSYBOX="$work/bb" /bin/sh "$work/stage.sh" /mnt/openipc-persist "$stage" "$work/SDK-INDEX" >&2
bb mount -t jffs2 -o ro /dev/mtdblock5 "$stock"
[ ! -e "$stock/openipc-persist" ] && [ ! -L "$stock/openipc-persist" ]
bb cp -a "$stock/." "$merged/"
bb cp -a "$stage/openipc-persist" "$merged/"
# Keep a manifest of the pre-existing stock files to prove they survive migration.
(cd "$stock" && bb find . -type f -exec "$work/bb" sha256sum '{}' ';') > "$merged/STOCK-SOURCE-SHA256SUMS"
(cd "$merged" && bb tar -cf - .)
'''
    tree = out/'private-config-tree'
    modes = extract_private(ssh(args.camera, '/bin/sh', collection.encode()), tree)
    save_json(out/'PRIVATE-ORIGINAL-MODES.json', modes)
    for p in tree.rglob('*'):
        if not p.is_symlink():
            p.chmod(0o700 if p.is_dir() else 0o600)
    finish_private(candidate, out, report, tree, modes)


def finish_private(candidate, out, report, tree, modes):
    stock_manifest = tree/'STOCK-SOURCE-SHA256SUMS'
    stock_files = []
    for row in stock_manifest.read_text().splitlines():
        checksum, name = row.split(maxsplit=1)
        name = name.removeprefix('./')
        assert not name.startswith('/') and '..' not in PurePosixPath(name).parts
        assert sha((tree/name).read_bytes()) == checksum
        stock_files.append(name)
    shutil.copyfile(stock_manifest, out/'STOCK-SOURCE-SHA256SUMS')
    stock_manifest.unlink()
    expected = tree_records(tree)
    for name in expected:
        expected[name]['mode'] = modes[name]
    devtable = out/'private-modes.devtable'
    devtable.write_text(''.join(f'/{name} {"d" if stat.S_ISDIR(e["mode"]) else "f"} {e["mode"] & 0o7777:o} 0 0 - - - - -\n' for name, e in expected.items() if not stat.S_ISLNK(e['mode'])))
    # All entries outside the new subtree are retained from the read-only stock copy.
    stock_entries = [n for n in expected if not n.startswith('openipc-persist')]
    assert len(stock_files) >= 15 and {n for n in stock_entries if not expected[n].get('directory')} == set(stock_files)
    image_dir = out/'install'
    image_dir.mkdir(mode=0o700)
    for leaf in ['rootfs-mtd3.bin', 'app-mtd4.bin']:
        shutil.copyfile(candidate/'images'/leaf, image_dir/leaf)
    config_image = image_dir/'config-mtd5.jffs2'
    subprocess.run(['mkfs.jffs2', '--root', str(tree), '--output', str(config_image),
                    '--little-endian', '--eraseblock=4096', '--pagesize=2048',
                    '--compression-mode=none', '--squash-uids', '--devtable', str(devtable), '--pad=393216'], check=True)
    config = pack_4k_nodes(config_image.read_bytes())
    config_image.write_bytes(config)
    assert len(config) == 0x60000
    config_result = verify_jffs2(config, expected)
    config_result['packing'] = 'unaltered mkfs inode/dirent nodes repacked into physical 4 KiB eraseblocks'
    assert config_result['remaining_before_kernel_reserve_bytes'] >= 0x18000
    # Minimize secret duplication: remove the unpacked private tree after verification.
    save_json(out/'PRIVATE-TREE-MANIFEST.json', expected)
    shutil.rmtree(tree)
    devtable.unlink()
    report.update(private_configuration_image=config_result,
                  stock_entries_preserved=len(stock_entries),
                  original_backup_sha256=ORIGINAL_SHA,
                  candidate_artifacts_sha256_verified=True,
                  pending=['physical recovery availability/validation',
                           'authorization for mtd3+mtd4+mtd5 installation',
                           'last-minute source/configuration freshness check',
                           'JFFS2 kernel mount and internal boot after controlled installation',
                           'cold boot without SD after installation'])
    save_json(out/'PREPARATION.json', report)
    rows = []
    for p in sorted(out.rglob('*')):
        if p.is_file() and p.name != 'SHA256SUMS':
            rows.append(f'{sha(p.read_bytes())}  {p.relative_to(out).as_posix()}')
    (out/'SHA256SUMS').write_text('\n'.join(rows)+'\n')
    print(json.dumps({'output': str(out), 'double_NOR_read': 'identical',
                      'stock_entries_preserved': len(stock_entries),
                      'config_image': config_result, 'NOR_written': False}, indent=2))


if __name__ == '__main__':
    main()
