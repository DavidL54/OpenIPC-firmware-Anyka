#!/usr/bin/env python3
"""Build local mtd3/mtd4 candidates. No camera access or flash command."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
from elf_runtime import assert_same_runtime

BASE = Path(__file__).resolve().parent.parent
HERE = BASE / "internal-firmware"
ROOT_LIMIT, APP_LIMIT = 0xF0000, 0x4A0000
ORIGINAL_SHA = "2794e0c675cbb56c76e56285160eda580b89f29cd67c41f08f971c121b674e60"
ROOT_SHA = "c9614459dadc1d02157db394f995617053c02e5ccb8c4b2fdb94a2d2ebcf6dcb"
SDK_SHA = "21ba308bac3d1ef8379e149bc32cedd29e71e19335af01280aaf21bd665bca6b"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, **kw):
    return subprocess.run([str(v) for v in args], check=True, **kw)


def write(path, text, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)


def link(path, target):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or path.exists():
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()
    path.symlink_to(target)


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gateway", type=Path, required=True)
    parser.add_argument("--busybox", type=Path)
    parser.add_argument("--app-block", type=int, choices=[65536, 131072, 262144, 524288, 1048576], default=524288)
    parser.add_argument("--headless", action="store_true", help="Omit OpenIPC UI; keep authenticated native camera API backend")
    parser.add_argument("--gateway-region", action="store_true", help="Place a gzip gateway filesystem after the XZ SDK filesystem inside mtd4")
    parser.add_argument("--sdk-region-bytes", type=lambda value: int(value, 0), default=0x268000)
    parser.add_argument("--app-compression", choices=["xz", "gzip"], default="xz")
    parser.add_argument("--strip-runtime-symbols", action="store_true",
                        help="Remove non-runtime ELF symbols/debug data; verify loaded contents unchanged")
    args = parser.parse_args()
    if args.gateway_region:
        assert args.headless and args.app_compression == "xz"
        assert 0 < args.sdk_region_bytes < APP_LIMIT and args.sdk_region_bytes % 4096 == 0
    app_limit = args.sdk_region_bytes if args.gateway_region else APP_LIMIT
    profile = None
    profile_file = args.gateway.resolve().parent / "PROFILE.json"
    if args.headless or profile_file.exists():
        profile = json.loads(profile_file.read_text())
        assert profile["profile"] in {"native-headless", "native-webui"} and profile["gateway_sha256"] == sha(args.gateway.resolve())
        assert profile.get("ui", False) == (not args.headless), "Gateway/UI profile mismatch"
    native_c = bool(profile and profile.get("implementation") == "C")
    assert not (native_c and args.gateway_region), "The small C gateway does not require a separate filesystem"
    gateway_name = "anyka-gateway" if native_c else "go2rtc"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True, mode=0o700)
    if (out / "images").exists():
        raise SystemExit("Existing image directory preserved; choose a fresh output directory")
    images = out / "images"
    images.mkdir(mode=0o700)
    tree = out / "staging"
    tree.mkdir(mode=0o700)
    root, app = tree / "rootfs", tree / "app"
    original = BASE / "backup_original_nor.bin"
    stock_root = BASE / "analysis/rootfs-current-20261008.bin"
    assert original.stat().st_size == 0x800000 and sha(original) == ORIGINAL_SHA
    assert stock_root.stat().st_size == ROOT_LIMIT and sha(stock_root) == ROOT_SHA
    sdk = BASE / "sd-openipc-test/openipc-persist/sdk-rtsp"
    assert sha(sdk / "SHA256SUMS") == SDK_SHA, "SDK baseline changed; review before building"
    manifest = []
    for row in (sdk / "SHA256SUMS").read_text().splitlines():
        digest, name = row.split(maxsplit=1)
        name = name.removeprefix("*").removeprefix("./")
        assert re.fullmatch(r"[A-Za-z0-9_./+\-]+", name)
        assert not name.startswith("/") and ".." not in PurePosixPath(name).parts
        leaf = PurePosixPath(name).name
        assert not (leaf in {"web-access.json", "ptz-presets.json", "authorized_keys", "dropbear_ed25519_host_key"}
                    or leaf.startswith("wpa_supplicant") or leaf.endswith(".ha1")
                    or leaf.endswith(".conf") or "ssh" in PurePosixPath(name).parts), "Private file in public SDK baseline"
        src = sdk / name
        assert src.is_file() and not src.is_symlink() and sha(src) == digest
        manifest.append(name)
    run(["unsquashfs", "-no-progress", "-d", root, stock_root, "bin", "lib", "etc", "usr", "sbin"], stdout=subprocess.DEVNULL)
    # No stock cloud application runs in this image. Keep both compatible libc
    # runtimes; proprietary SDK processes retain their original uClibc loader.
    keep = {"ld-uClibc-1.0.31.so", "ld-uClibc.so.0", "ld-uClibc.so.1",
            "libuClibc-1.0.31.so", "libc.so.0", "libc.so.1", "libgcc_s.so.1"}
    # uClibc loads this lazily for pthread_cancel (speaker shutdown). It is
    # absent from DT_NEEDED, so a normal ELF dependency scan cannot retain it.
    assert sha(root / "lib/libgcc_s.so.1") == "b8b875a1b1c35dcb9c6aa6b67932b2e2c1a92a461181c4fcd27b9eefa507b8ff"
    for p in (root / "lib").iterdir():
        if p.name not in keep:
            if p.is_dir() and not p.is_symlink(): shutil.rmtree(p)
            else: p.unlink()
    for p in (root / "etc/init.d").iterdir():
        if p.is_dir() and not p.is_symlink(): shutil.rmtree(p)
        else: p.unlink()
    with tempfile.TemporaryDirectory(prefix="anyka-internal-inputs-") as scratch_name:
        scratch = Path(scratch_name)
        musl = scratch / "musl"
        extract = ["bin/busybox", "lib/libc.so", "usr/bin/haserl", "usr/bin/ipcinfo",
             "usr/sbin/dropbear", "usr/sbin/wpa_supplicant", "usr/sbin/wpa_cli",
             "usr/lib/libnl*", "etc/ssl/certs/ca-certificates.crt"]
        if native_c and not args.headless:
            extract += ["usr/bin/curl", "usr/lib/libcurl*", "usr/lib/libmbed*", "usr/lib/libz.*"]
        run(["unsquashfs", "-no-progress", "-d", musl, BASE / "sd-openipc-test/openipc-sd.sqf"] + extract, stdout=subprocess.DEVNULL)
        shutil.copy2(args.busybox.resolve() if args.busybox else musl / "bin/busybox", root / "bin/busybox")
        (root / "bin/busybox").chmod(0o755)
        shutil.copy2(musl / "lib/libc.so", root / "lib/libc.so")
        (root / "lib/libc.so").chmod(0o755)
        link(root / "lib/ld-musl-arm.so.1", "libc.so")
        tools = app / "tools"
        shutil.copytree(musl, tools)
        if native_c and not args.headless:
            clock = args.gateway.resolve().parent / "anyka-clock-compat.so"
            assert sha(clock) == profile["clock_compat_sha256"]
            shutil.copy2(clock, tools / "usr/lib/anyka-clock-compat.so")
            for item in json.loads((HERE / "licenses/ORIGIN.json").read_text()):
                source = HERE / "licenses" / item["file"]
                assert sha(source) == item["sha256"]
                target = tools / "usr/share/licenses" / item["file"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        link(tools / "bin/busybox", "/bin/busybox")
        link(tools / "lib/libc.so", "/lib/libc.so")
        link(tools / "lib/ld-musl-arm.so.1", "/lib/ld-musl-arm.so.1")
        stock = scratch / "stock-app"
        run(["unsquashfs", "-no-progress", "-o", str(0x300000), "-d", stock, original,
             "ak/modules", "ak/sensor/isp_sc2b3ak_mipi_2lane_av130.conf",
             "drivers/gv-gpio.ko", "drivers/gv_step_motor.ko", "drivers/atbm6132c_wifi_usb.ko"], stdout=subprocess.DEVNULL)
        shutil.copytree(stock / "ak", app / "ak")
        shutil.copytree(stock / "drivers", app / "drivers")
    for p in (app / "ak/modules").glob("sensor_*.ko"):
        if p.name != "sensor_sc3336.ko": p.unlink()
    runtime = app / "runtime/sdk-rtsp"
    for name in manifest:
        target = runtime / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(sdk / name, target)
    # FAT implicitly supplied executable bits in the SD runtime. SquashFS
    # preserves the source modes, so normalize CGI entrypoints and shell
    # helpers explicitly (some source files are intentionally stored 0644).
    for p in runtime.rglob("*"):
        if p.is_file() and (p.suffix in {".cgi", ".sh"} or p.name == "openipc-sh"):
            p.chmod(0o755)
    shutil.copy2(args.gateway.resolve(), runtime / "go2rtc")
    if native_c:
        (runtime / "go2rtc").rename(runtime / gateway_name)
        manifest = [gateway_name if name == "go2rtc" else name for name in manifest]
        for folder, label in [(BASE / "native-gateway/vendor/cjson", "cjson"),
                              (BASE / "native-gateway/vendor/mbedtls", "mbedtls"),
                              (BASE / "native-gateway/vendor/go2rtc", "go2rtc")]:
            for p in folder.rglob("*"):
                if p.is_file() and (p.name == "LICENSE" or "LICENSES" in p.parts):
                    target = runtime / "licenses/native-gateway" / label / p.relative_to(folder)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(p, target)
    # Every SDK library and NPU model remains present; no experimental pruning.
    for p in (sdk / "lib").iterdir():
        assert p.is_file() and (runtime / "lib" / p.name).is_file()
        assert sha(p) == sha(runtime / "lib" / p.name), "SDK library changed"
    stripped = []
    if args.strip_runtime_symbols:
        for p in sorted(runtime.rglob("*.so")):
            before = p.read_bytes()
            run(["arm-linux-gnueabi-strip", "--strip-unneeded", p])
            after = p.read_bytes()
            assert_same_runtime(before, after)
            if before != after:
                stripped.append({"file": p.relative_to(runtime).as_posix(),
                                 "source_sha256": hashlib.sha256(before).hexdigest(),
                                 "image_sha256": sha(p), "saved_bytes": len(before) - len(after),
                                 "elf_runtime_contents_identical": True})
    for name in ["ak_venc_adapter.ko", "ak_venc_bridge.ko"]:
        link(app / "ak/modules" / name, "../../runtime/sdk-rtsp/" + name)
    link(app / "lib", "runtime/sdk-rtsp/lib")
    link(root / "media", "/app/tools")
    for directory in ["dev", "proc", "sys", "tmp", "var", "run", "mnt", "conf", "app", "opt"]:
        (root / directory).mkdir(exist_ok=True)
    (root / "tmp").chmod(0o1777)
    applets = subprocess.check_output(["qemu-arm-static", "-cpu", "arm926", "-L", str(root),
                                      str(root / "bin/busybox"), "--list-full"], text=True).splitlines()
    required = {"sh", "init", "login", "getty", "mdev", "mount", "ifconfig", "udhcpc", "httpd", "ntpd", "sha256sum"}
    assert required <= {PurePosixPath(p).name for p in applets}
    for name in applets:
        if name != "bin/busybox":
            path = root / name
            if not path.exists() and not path.is_symlink():
                link(path, os.path.relpath(root / "bin/busybox", path.parent))
    write(root / "etc/inittab", "::sysinit:/etc/init.d/rcS\nttySAK0::respawn:/sbin/getty -L ttySAK0 115200 vt100\n::restart:/sbin/init\n::ctrlaltdel:/sbin/reboot\n::shutdown:/bin/umount -a -r\n")
    write(root / "etc/fstab", "# Internal candidate mounts are managed explicitly by rcS.\n")
    link(root / "etc/resolv.conf", "/tmp/resolv.conf")
    for name in ["rcS", "internal-monitor.sh", "internal-check-view.sh"]:
        shutil.copy2(HERE / name, root / "etc/init.d" / name)
        (root / "etc/init.d" / name).chmod(0o755)
    if native_c:
        p = root / "etc/init.d/internal-monitor.sh"
        write(p, p.read_text().replace("go2rtc", gateway_name), 0o755)
    shutil.copy2(HERE / "load-drivers.sh", app / "ak/load.sh")
    (app / "ak/load.sh").chmod(0o755)
    config = {"api": {"listen": "127.0.0.1:1984", "allow_paths": ["/", "/api/ws"]},
              "rtsp": {"listen": ":554"}, "webrtc": {"listen": ""},
              "streams": {"preview": "rtsp://127.0.0.1:8555/vs1#backchannel=0",
                          "vs0": "rtsp://127.0.0.1:8555/vs0#backchannel=0",
                          "vs1": "rtsp://127.0.0.1:8555/vs1#backchannel=0"}}
    write(runtime / "go2rtc.json", json.dumps(config, separators=(",", ":")) + "\n")
    (runtime / "go2rtc.yaml").unlink()
    p = runtime / "start-web.sh"
    text = p.read_text()
    assert text.count('"$1/go2rtc.yaml"') == 1
    text = text.replace('"$1/go2rtc.yaml"', '"$1/go2rtc.json"')
    if args.headless or native_c:
        first = text.index("# Per-camera preference")
        after = text.index('$MUSL_LD --library-path "$MUSL_LIBS" "$BUSYBOX" ifconfig lo', first)
        text = text[:first] + "export ANYKA_WEB_UI=" + ("0" if args.headless else "1") + " ANYKA_NATIVE_RTSP=1\n" + text[after:]
        text = text.replace('    export ANYKA_NATIVE_RTSP="${ANYKA_NATIVE_RTSP:-1}"', '    export ANYKA_NATIVE_RTSP=1')
        text = text.replace('exec "$1/go2rtc" -config "$1/go2rtc.json"', 'exec "$1/go2rtc"')
        (runtime / "go2rtc.json").unlink()
        if native_c:
            text = text.replace('    export GOMEMLIMIT=6MiB GOGC=100 GOMAXPROCS=1\n', '')
            text = text.replace('exec "$1/go2rtc"', 'exec "$1/' + gateway_name + '"')
            text = text.replace('/tmp/go2rtc.log', '/tmp/anyka-gateway.log')
    if native_c:
        text = text.replace("# ANYKA_PTZ_STARTUP_HOOK", '''ANYKA_ACCESS_FILE="$SDK_DIR/../web-access.json" "$MUSL_LD" --library-path "$MUSL_LIBS" "$SDK_DIR/anyka-gateway" --ptz-startup </dev/null >/tmp/ptz-startup.log 2>&1 &''')
    write(p, text, 0o755)
    if native_c and not args.headless:
        for name in ["live.page", "extensions.page", "p/anyka.sh", "access.page", "tools.page", "p/logs.sh", "logs.page"]:
            p = runtime / "www/cgi-bin" / name
            write(p, p.read_text().replace("go2rtc ARMv5 / H.264 MSE", "Native C / H.264 MSE")
                  .replace("authenticated Go gateway", "authenticated C gateway")
                  .replace("saved on the SD card", "saved in internal memory")
                  .replace("password hash on SD", "password hash in internal memory")
                  .replace("/tmp/go2rtc.log", "/tmp/anyka-gateway.log")
                  .replace("log_file Relay", "log_file Gateway")
                  .replace("Preview relay", "Camera gateway")
                  .replace('group_head "SD Card"', 'group_head "Storage"'), 0o755)
        (runtime / "anyka-audio-play").unlink()
        manifest = [name for name in manifest if (runtime / name).is_file()]
    p = runtime / "www/cgi-bin/p/anyka.sh"
    write(p, p.read_text().replace("fw_variant='Anyka SD'", "fw_variant='Anyka interne'")
          .replace("fw_build='Stock kernel / SD runtime'", "fw_build='Stock kernel / internal runtime'"), 0o755)
    p = runtime / "www/cgi-bin/p/image.sh"
    write(p, p.read_text().replace("SD setting unavailable", "Internal setting unavailable")
          .replace("SD read-only remount failed", "Internal read-only remount failed"), 0o755)
    # Only omit assets unreachable from every shipped page/helper and from the
    # transitive references of those assets. Keep licenses independently of links.
    # The native camera UI does not expose the legacy Majestic/RAW/SD editors.
    web = runtime / "www"
    assets = {p.name: p for p in (web / "a").iterdir() if p.is_file()}
    reachable = set()
    texts = "\n".join(p.read_text(errors="replace") for p in web.rglob("*")
                      if p.is_file() and web / "a" not in p.parents)
    while True:
        new = {name for name in assets if name in texts} - reachable
        if not new: break
        reachable.update(new)
        texts += "\n" + "\n".join(assets[name].read_text(errors="replace") for name in new)
    removed_assets = []
    for name, p in sorted(assets.items()):
        if name not in reachable and "LICENSE" not in name.upper():
            removed_assets.append({"file": "www/a/" + name, "bytes": p.stat().st_size, "sha256": sha(p)})
            p.unlink()
    write(out / "ASSET-AUDIT.json", json.dumps({"method": "transitive literal-filename references from all non-asset www files",
          "browser_validation": False, "retained": sorted(reachable), "omitted": removed_assets}, indent=2) + "\n", 0o600)
    removed_headless = []
    if args.headless:
        keep_web = {"cgi-bin/ptz.cgi", "cgi-bin/gpio.cgi", "cgi-bin/isp.cgi", "cgi-bin/snapshot.cgi",
                    "cgi-bin/p/ptz.sh", "cgi-bin/p/gpio.sh", "cgi-bin/p/image.sh"}
        for p in sorted(web.rglob("*")):
            if not p.is_file() or p.relative_to(web).as_posix() in keep_web:
                continue
            # Retain third-party license notices without the browser assets.
            if "LICENSE" in p.name.upper():
                target = runtime / "licenses" / p.relative_to(web)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, target)
            removed_headless.append({"file": "www/" + p.relative_to(web).as_posix(), "bytes": p.stat().st_size, "sha256": sha(p)})
            p.unlink()
        for p in sorted(web.rglob("*"), key=lambda p: len(p.parts), reverse=True):
            if p.is_dir() and not any(p.iterdir()): p.rmdir()
        for p in [app / "tools/usr/bin/haserl", app / "tools/usr/bin/ipcinfo", runtime / "anyka-audio-play"]:
            if p.exists():
                removed_headless.append({"file": p.relative_to(app).as_posix(), "bytes": p.stat().st_size, "sha256": sha(p)})
                p.unlink()
        manifest = [name for name in manifest if (runtime / name).is_file()]
        assert not list(web.rglob("*.page")) and not list(web.rglob("*.js")) and not list(web.rglob("*.html"))
        write(out / "HEADLESS-AUDIT.json", json.dumps({"gateway": profile, "omitted": removed_headless,
              "backend_cgi_retained": sorted(keep_web), "ui_port_1984": False}, indent=2) + "\n", 0o600)
    # The check runs from the writable directory view, not the immutable payload.
    # All directories in that view are real directories; CGI ../../ paths must
    # continue to refer to the persistent per-camera directory.
    write(runtime / "SHA256SUMS", "".join(sha(p) + "  ./" + p.relative_to(runtime).as_posix() + "\n"
          for p in sorted(runtime.rglob("*")) if p.is_file() and p.name != "SHA256SUMS"))
    entries = []
    for p in sorted(runtime.rglob("*")):
        entries.append(("D" if p.is_dir() else "F") + " " + p.relative_to(runtime).as_posix())
    write(app / "runtime/SDK-INDEX", "\n".join(entries) + "\n")
    gateway_region = None
    if args.gateway_region:
        gateway_tree = out / "gateway-staging"
        gateway_tree.mkdir()
        shutil.copy2(args.gateway.resolve(), gateway_tree / "go2rtc")
        gateway_image = images / "gateway.squashfs"
        run(["mksquashfs", gateway_tree, gateway_image, "-noappend", "-comp", "gzip", "-b", "65536",
             "-processors", "1", "-no-progress", "-all-root", "-mkfs-time", "0"], stdout=subprocess.DEVNULL)
        assert gateway_image.stat().st_size <= APP_LIMIT - args.sdk_region_bytes, "Gateway gzip region does not fit"
        gateway_region = {"offset": args.sdk_region_bytes, "bytes": gateway_image.stat().st_size,
                          "compression": "gzip", "block": 65536, "image_sha256": sha(gateway_image)}
        # A host-only view for manifest/ELF checks. It is excluded from rootfs.
        dynamic = root / "run/gateway"
        dynamic.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.gateway.resolve(), dynamic / "go2rtc")
        link(runtime / "go2rtc", "/run/gateway/go2rtc")
        write(app / "runtime/GATEWAY-REGION.json", json.dumps(gateway_region, indent=2) + "\n")
        p = root / "etc/init.d/rcS"
        text = p.read_text()
        marker = '(cd /app && bb sha256sum -c SHA256SUMS >/dev/null)'
        assert text.count(marker) == 1
        text = text.replace(marker, 'bb mkdir -p /run/gateway\n'
            + 'bb mount -t squashfs -o loop,ro,offset=' + str(args.sdk_region_bytes)
            + ' /dev/mtdblock4 /run/gateway\n' + marker)
        write(p, text, 0o755)
    # Read-only image namespace resolver; never follow an absolute image link
    # through the host filesystem when hashing relocated payloads.
    def resolve_image(virtual):
        parts = list(PurePosixPath(virtual).parts[1:])
        resolved = []
        hops = 0
        while parts:
            part = parts.pop(0)
            if part in ["", "."]: continue
            if part == "..":
                assert resolved
                resolved.pop()
                continue
            prefix = "/" + "/".join(resolved + [part])
            host = app.joinpath(*resolved[1:], part) if resolved and resolved[0] == "app" else root.joinpath(*resolved, part)
            if host.is_symlink():
                target = os.readlink(host)
                hops += 1
                assert hops < 40, "Image symlink loop"
                if target.startswith("/"): resolved = []
                parts = list(PurePosixPath(target).parts[(1 if target.startswith('/') else 0):]) + parts
            else:
                resolved.append(part)
        return app.joinpath(*resolved[1:]) if resolved and resolved[0] == "app" else root.joinpath(*resolved)
    def app_manifest():
        rows = []
        for p in sorted(app.rglob("*")):
            if p == app / "SHA256SUMS" or p.is_dir() and not p.is_symlink(): continue
            target = resolve_image("/app/" + p.relative_to(app).as_posix())
            if target.is_dir(): continue
            assert target.is_file(), str(p)
            rows.append(sha(target) + "  " + p.relative_to(app).as_posix() + "\n")
        write(app / "SHA256SUMS", "".join(rows))
    pseudo = out / "root-devices.pf"
    pseudo.write_text("dev/console c 0600 0 0 5 1\ndev/null c 0666 0 0 1 3\ndev/zero c 0666 0 0 1 5\ndev/tty c 0666 0 0 5 0\ndev/random c 0666 0 0 1 8\ndev/urandom c 0666 0 0 1 9\n")
    def squash(source, image, block, devices=False, compression="xz"):
        command = ["mksquashfs", source, image, "-noappend", "-comp", compression,
                   "-b", str(block), "-processors", "1", "-no-progress", "-all-root", "-mkfs-time", "0"]
        if compression == "xz": command += ["-Xbcj", "arm"]
        if devices: command += ["-pf", pseudo]
        if args.gateway_region and source == root: command += ["-e", "run/gateway"]
        run(command, stdout=subprocess.DEVNULL)
        return image.stat().st_size
    root_image, app_image = images / "rootfs.squashfs", images / "app.squashfs"
    app_manifest()
    root_size = squash(root, root_image, 65536, True)
    app_size = squash(app, app_image, args.app_block, compression=args.app_compression)
    print(f"Initial sizes: rootfs {root_size}/{ROOT_LIMIT}; SDK/application {app_size}/{app_limit}", flush=True)
    # Fill spare rootfs space with immutable payload files while retaining their
    # application paths as links. Both libcs and all SDK libraries are retained.
    relocated = []
    candidates = sorted((p for p in app.rglob("*") if p.is_file() and not p.is_symlink()
                         and p.name not in {"SHA256SUMS", "SDK-INDEX", "go2rtc", "go2rtc.json"}
                         and p.stat().st_size > 8192), key=lambda p: p.stat().st_size, reverse=True)
    for p in candidates:
        if root_size >= ROOT_LIMIT or app_size <= app_limit: break
        relative = p.relative_to(app)
        moved = root / "opt/internal-payload" / relative
        moved.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(p, moved)
        p.symlink_to("/opt/internal-payload/" + relative.as_posix())
        trial_root = squash(root, root_image, 65536, True)
        if trial_root > ROOT_LIMIT:
            p.unlink()
            shutil.move(moved, p)
            continue
        root_size = trial_root
        relocated.append(relative.as_posix())
        app_manifest()
        app_size = squash(app, app_image, args.app_block, compression=args.app_compression)
        print(f"Relocated {relative}: rootfs {root_size}; app {app_size}", flush=True)
    root_size = squash(root, root_image, 65536, True)
    app_manifest()
    app_size = squash(app, app_image, args.app_block, compression=args.app_compression)
    sdk_size = app_size
    if args.gateway_region and sdk_size <= app_limit:
        sdk_bytes = app_image.read_bytes()
        (images / "sdk.squashfs").write_bytes(sdk_bytes)
        app_image.write_bytes(sdk_bytes + b"\xff" * (app_limit - sdk_size) + gateway_image.read_bytes())
        app_size = app_image.stat().st_size
    report = {"status": "candidate-not-hardware-validated", "hardware_boot_validated": False,
              "installable": False, "stock_kernel_dtb_bootloader_unchanged": True,
              "private_camera_settings_embedded": False,
              "requires_internal_configuration_provisioning": True,
              "all_sdk_libraries_preserved": True,
              "runtime_symbol_stripping": stripped,
              "unused_web_assets_omitted": removed_assets,
              "rootfs": {"bytes": root_size, "limit": ROOT_LIMIT, "block": 65536},
              "app": {"bytes": app_size, "limit": APP_LIMIT, "block": args.app_block, "compression": args.app_compression},
              "headless": args.headless, "headless_files_omitted": removed_headless,
              "gateway_implementation": "C" if native_c else "Go", "gateway_runtime_name": gateway_name,
              "gateway_region": gateway_region, "sdk_region_bytes": sdk_size, "sdk_region_limit": app_limit,
              "relocated_payloads": relocated, "gateway_sha256": sha(args.gateway.resolve()),
              "busybox_sha256": sha(root / "bin/busybox"),
              "inputs": {"stock_nor_sha256": ORIGINAL_SHA, "stock_rootfs_sha256": ROOT_SHA, "sdk_manifest_sha256": SDK_SHA},
              "missing_validation": ["boot on stock kernel", "private configuration migration and JFFS2 free space",
                                     "recovery procedure", "video/audio/HA controls" if args.headless else "video/audio/preview/HA controls",
                                     "memory under video/audio/NPU load", "physical cold boot without SD"]}
    write(out / "BUILD-REPORT.json", json.dumps(report, indent=2) + "\n", 0o600)
    if root_size > ROOT_LIMIT or app_size > APP_LIMIT or sdk_size > app_limit:
        raise SystemExit("Size gate failed; no padded image was produced. See BUILD-REPORT.json.")
    # Local structural checks on the final image trees before packaging.
    for p in [root / "etc/init.d/rcS", root / "etc/init.d/internal-monitor.sh", root / "etc/init.d/internal-check-view.sh",
              app / "ak/load.sh", runtime / "start.sh", runtime / "start-web.sh"]:
        run(["qemu-arm-static", "-cpu", "arm926", "-L", root, root / "bin/busybox", "sh", "-n", p])
    assert resolve_image("/sbin/init").read_bytes() == (root / "bin/busybox").read_bytes()
    assert resolve_image("/media/lib/libc.so").read_bytes() == (root / "lib/libc.so").read_bytes()
    # Every ELF dependency must have an image provider. Never execute the SDK on
    # the host: its device-dependent constructors are only for camera validation.
    providers = {p.name for folder in [root / "lib", app / "tools/usr/lib", runtime / "lib"] for p in folder.iterdir()}
    dependencies = set()
    for virtual in ["/app/runtime/sdk-rtsp/" + n for n in manifest if n.endswith('.so') or n in {'ak_rtsp_server_sample', 'anyka-audio-play', gateway_name}]:
        p = resolve_image(virtual)
        data = subprocess.check_output(["readelf", "-d", p], text=True)
        dependencies.update(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", data))
    assert dependencies <= providers, "Missing ELF providers: " + repr(dependencies - providers)
    for name, data, limit in [("rootfs-mtd3.bin", root_image.read_bytes(), ROOT_LIMIT),
                              ("app-mtd4.bin", app_image.read_bytes(), APP_LIMIT)]:
        (images / name).write_bytes(data + b"\xff" * (limit - len(data)))
    (images / "internal-system-mtd3-mtd4.bin").write_bytes((images / "rootfs-mtd3.bin").read_bytes() + (images / "app-mtd4.bin").read_bytes())
    shutil.copy2(HERE / "stage-private-config.sh", out / "stage-private-config.sh")
    shutil.copy2(app / "runtime/SDK-INDEX", out / "SDK-INDEX")
    files = sorted(p for p in images.iterdir() if p.is_file())
    write(out / "SHA256SUMS", "".join(sha(p) + "  images/" + p.name + "\n" for p in files) +
          sha(out / "stage-private-config.sh") + "  stage-private-config.sh\n" +
          sha(out / "SDK-INDEX") + "  SDK-INDEX\n", 0o600)
    report["structural_checks"] = {"arm926_shell_syntax": "passed", "sdk_elf_dependency_providers": "passed",
                                    "separate_libc_loaders": "passed", "size_gate": "passed"}
    report["partition_image"] = {"file": "images/internal-system-mtd3-mtd4.bin", "bytes": ROOT_LIMIT + APP_LIMIT,
                                 "nor_offset": "0x210000", "not_a_full_nor_image": True}
    write(out / "BUILD-REPORT.json", json.dumps(report, indent=2) + "\n", 0o600)
    print("Images built; hardware validation and private configuration provisioning still required.", flush=True)


if __name__ == "__main__":
    main()
