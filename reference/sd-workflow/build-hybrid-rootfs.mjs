import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const base = path.dirname(fileURLToPath(import.meta.url));
const original = path.join(base, 'analysis/rootfs-current-20261008.bin');
const destination = path.join(base, 'recovery-build/hybrid-rootfs');
const candidate = path.join(destination, 'rootfs-hybrid.squashfs');
assert(!fs.existsSync(candidate), 'Candidate already exists; preserve it before rebuilding');
assert.equal(fs.statSync(original).size, 983040);
assert.equal(crypto.createHash('sha256').update(fs.readFileSync(original)).digest('hex'),
    'c9614459dadc1d02157db394f995617053c02e5ccb8c4b2fdb94a2d2ebcf6dcb',
    'Source must match the current camera rootfs backup');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'anyka-hybrid-rootfs-'));

function execute(program, args) {
    const result = spawnSync(program, args, {encoding: 'utf8', maxBuffer: 8 * 1024 * 1024});
    assert.equal(result.status, 0, `${program}: ${result.stderr || result.stdout}`);
    return result.stdout;
}

function quote(filename) {
    return "'" + filename.replaceAll("'", "'\\''") + "'";
}

try {
    const pseudo = path.join(work, 'stock.pf');
    execute('unsquashfs', ['-no-progress', '-pf', pseudo, original]);
    const exported = fs.readFileSync(pseudo);
    const marker = exported.indexOf(Buffer.from('# START OF DATA - DO NOT MODIFY'));
    assert(marker > 0);
    const header = exported.subarray(0, marker).toString();
    assert.equal(header.match(/^etc\/init\.d\/rc\.local R .+$/gm)?.length, 1);
    const renamed = header.replace(/^etc\/init\.d\/rc\.local (R .+)$/m, 'etc/init.d/rc.local.stock $1');
    fs.writeFileSync(pseudo, Buffer.concat([Buffer.from(renamed), exported.subarray(marker)]));
    const image = path.join(work, 'candidate.squashfs');
    const empty = path.join(work, 'empty');
    fs.mkdirSync(empty);
    const root = header.match(/^\/ D (\d+) ([0-7]+) (\d+) (\d+)$/m);
    assert(root && root[3] === '0' && root[4] === '0', 'Unexpected stock root ownership');
    fs.chmodSync(empty, Number.parseInt(root[2], 8));
    fs.utimesSync(empty, Number(root[1]), Number(root[1]));
    execute('mksquashfs', [empty, image, '-pf', pseudo, '-noappend', '-comp', 'xz', '-b', '65536',
        '-processors', '1', '-no-progress', '-mkfs-time', '0', '-all-root',
        '-p', `etc/init.d/rc.local f 0755 0 0 cat ${quote(path.join(base, 'hybrid-rc.local.sh'))}`,
        '-p', `etc/init.d/hybrid-boot.sh f 0755 0 0 cat ${quote(path.join(base, 'hybrid-boot-prototype.sh'))}`]);
    const bytes = fs.statSync(image).size;
    assert(bytes <= 983040, 'Candidate exceeds mtd3');
    const read = filename => {
        const result = spawnSync('unsquashfs', ['-cat', image, filename], {maxBuffer: 4 * 1024 * 1024});
        assert.equal(result.status, 0);
        return result.stdout;
    };
    for (const [inside, outside] of [['etc/init.d/rc.local', 'hybrid-rc.local.sh'],
        ['etc/init.d/hybrid-boot.sh', 'hybrid-boot-prototype.sh']]) {
        assert(read(inside).equals(fs.readFileSync(path.join(base, outside))));
    }
    const saved = spawnSync('unsquashfs', ['-cat', original, 'etc/init.d/rc.local']);
    assert.equal(saved.status, 0);
    assert(read('etc/init.d/rc.local.stock').equals(saved.stdout));
    fs.mkdirSync(destination, {recursive: true, mode: 0o700});
    fs.chmodSync(destination, 0o700);
    fs.copyFileSync(image, candidate);
    fs.chmodSync(candidate, 0o600);
    const rollback = path.join(destination, 'rootfs-stock-mtd3.bin');
    fs.copyFileSync(original, rollback);
    fs.chmodSync(rollback, 0o600);
    const hashes = [candidate, rollback].map(filename =>
        `${crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex')}  ${path.basename(filename)}`);
    fs.writeFileSync(path.join(destination, 'SHA256SUMS'), hashes.join('\n') + '\n');
    const staging = path.join(work, 'staging');
    const payload = path.join(staging, 'hybrid-install');
    fs.mkdirSync(path.join(payload, 'etc-init'), {recursive: true});
    const files = ['rootfs-hybrid.squashfs', 'rootfs-stock-mtd3.bin',
        'etc-init/rc.local', 'etc-init/rc.local.stock', 'etc-init/hybrid-boot.sh', 'etc-init/rcS'];
    fs.copyFileSync(candidate, path.join(payload, files[0]));
    fs.copyFileSync(rollback, path.join(payload, files[1]));
    for (const filename of files.slice(2)) {
        const inside = 'etc/init.d/' + path.basename(filename);
        fs.writeFileSync(path.join(payload, filename), read(inside), {mode: 0o755});
    }
    const payloadHashes = files.map(filename =>
        `${crypto.createHash('sha256').update(fs.readFileSync(path.join(payload, filename))).digest('hex')}  ${filename}`);
    fs.writeFileSync(path.join(payload, 'SHA256SUMS'), payloadHashes.join('\n') + '\n');
    const archive = path.join(destination, 'hybrid-install-private.tar');
    execute('tar', ['-cf', archive, '-C', staging, 'hybrid-install']);
    fs.chmodSync(archive, 0o600);
    const archiveHash = crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex');
    fs.writeFileSync(path.join(destination, 'hybrid-install-private.tar.sha256'),
        `${archiveHash}  hybrid-install-private.tar\n`);
    console.log(`Private SD/RAM archive SHA256: ${archiveHash}`);
    console.log(`Candidate: ${bytes} bytes; mtd3 spare: ${983040 - bytes} bytes`);
    console.log('Stock init preserved; no camera access or NOR write. Hardware validation still required.');
} finally {
    fs.rmSync(work, {recursive: true, force: true});
}