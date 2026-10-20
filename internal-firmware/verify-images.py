#!/usr/bin/env python3
"""Independently extract and verify a local candidate, without camera access."""
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
from elf_runtime import assert_same_runtime


def digest(data): return hashlib.sha256(data).hexdigest()


def main():
    out = Path(sys.argv[1]).resolve()
    report = json.loads((out / "BUILD-REPORT.json").read_text())
    gateway_name = report.get("gateway_runtime_name", "go2rtc")
    root_raw = (out / "images/rootfs.squashfs").read_bytes()
    app_raw = (out / "images/app.squashfs").read_bytes()
    system = (out / "images/internal-system-mtd3-mtd4.bin").read_bytes()
    assert len(system) == 0x590000
    assert system[:len(root_raw)] == root_raw
    assert system[len(root_raw):0xf0000] == b"\xff" * (0xf0000 - len(root_raw))
    assert system[0xf0000:0xf0000 + len(app_raw)] == app_raw
    assert system[0xf0000 + len(app_raw):] == b"\xff" * (0x4a0000 - len(app_raw))
    for row in (out / "SHA256SUMS").read_text().splitlines():
        expected, name = row.split(maxsplit=1)
        assert digest((out / name).read_bytes()) == expected, name
    nodes = subprocess.check_output(["unsquashfs", "-ll", str(out / "images/rootfs.squashfs"), "dev"], text=True)
    assert re.search(r'^c\S+\s+root/root\s+5,\s*1\s+.*squashfs-root/dev/console$', nodes, re.M)
    assert re.search(r'^c\S+\s+root/root\s+1,\s*3\s+.*squashfs-root/dev/null$', nodes, re.M)
    with tempfile.TemporaryDirectory(prefix="anyka-image-verify-") as work:
        root = Path(work) / "root"
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(root), str(out / "images/rootfs.squashfs"),
                        "bin", "sbin", "usr", "lib", "etc", "opt", "media", "tmp", "var", "mnt", "conf"],
                       stdout=subprocess.DEVNULL, check=True)
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(root / "app"), str(out / "images/app.squashfs")],
                       stdout=subprocess.DEVNULL, check=True)
        region = report.get("gateway_region")
        if region:
            sdk_raw = (out / "images/sdk.squashfs").read_bytes()
            gateway_raw = (out / "images/gateway.squashfs").read_bytes()
            offset = region["offset"]
            assert len(sdk_raw) <= offset < 0x4a0000 and offset % 4096 == 0
            assert offset + len(gateway_raw) <= 0x4a0000
            assert app_raw[:len(sdk_raw)] == sdk_raw
            assert app_raw[len(sdk_raw):offset] == b"\xff" * (offset - len(sdk_raw))
            assert app_raw[offset:] == gateway_raw
            assert digest(gateway_raw) == region["image_sha256"]
            assert "offset=" + str(offset) in (root / "etc/init.d/rcS").read_text()
            (root / "run").mkdir(exist_ok=True)
            subprocess.run(["unsquashfs", "-no-progress", "-d", str(root / "run/gateway"), str(out / "images/gateway.squashfs")],
                           stdout=subprocess.DEVNULL, check=True)
        def resolve(name):
            virtual = posixpath.normpath("/" + name.lstrip("/"))
            for _ in range(40):
                parts = virtual.strip("/").split("/")
                for index in range(len(parts)):
                    p = root.joinpath(*parts[:index + 1])
                    if p.is_symlink():
                        destination = os.readlink(p)
                        virtual = posixpath.normpath(posixpath.join("/", *parts[:index], destination, *parts[index + 1:]))
                        break
                else:
                    return root / virtual.lstrip("/")
            raise AssertionError("Image symlink loop")
        checked = 0
        for folder in ["/app", "/app/runtime/sdk-rtsp"]:
            for row in resolve(folder + "/SHA256SUMS").read_text().splitlines():
                expected, name = row.split(maxsplit=1)
                assert digest(resolve(folder + "/" + name).read_bytes()) == expected, folder + "/" + name
                checked += 1
        assert "F SHA256SUMS" in (out / "SDK-INDEX").read_text().splitlines()
        assert not list((root / "mnt").glob("**/web-access.json"))
        for name in ["/sbin/init", "/bin/sh", "/media/bin/busybox"]:
            assert digest(resolve(name).read_bytes()) == digest((root / "bin/busybox").read_bytes())
        assert resolve("/lib/ld-uClibc.so.0") != resolve("/lib/ld-musl-arm.so.1")
        # pthread_cancel dlopens the stock unwinder at speaker shutdown.
        assert digest(resolve("/lib/libgcc_s.so.1").read_bytes()) == "b8b875a1b1c35dcb9c6aa6b67932b2e2c1a92a461181c4fcd27b9eefa507b8ff"
        if report.get("headless"):
            runtime = root / "app/runtime/sdk-rtsp"
            backend = {p.relative_to(runtime / "www").as_posix()
                       for p in (runtime / "www").rglob("*") if p.is_file()}
            assert backend == {"cgi-bin/ptz.cgi", "cgi-bin/gpio.cgi", "cgi-bin/isp.cgi",
                               "cgi-bin/snapshot.cgi", "cgi-bin/p/ptz.sh",
                               "cgi-bin/p/gpio.sh", "cgi-bin/p/image.sh"}
            for name in ["/app/tools/usr/bin/haserl", "/app/tools/usr/bin/ipcinfo",
                         "/app/runtime/sdk-rtsp/anyka-audio-play",
                         "/app/runtime/sdk-rtsp/go2rtc.yaml", "/app/runtime/sdk-rtsp/go2rtc.json"]:
                assert not resolve(name).exists(), name
            assert digest(resolve("/app/runtime/sdk-rtsp/" + gateway_name).read_bytes()) == report["gateway_sha256"]
            if report.get("gateway_implementation") == "C":
                assert not resolve("/app/runtime/sdk-rtsp/go2rtc").exists()
                assert 'anyka-gateway' in (root / "etc/init.d/internal-monitor.sh").read_text()
        if report.get("gateway_implementation") == "C" and not report.get("headless"):
            for name in ["/app/runtime/sdk-rtsp/www/cgi-bin/access.page", "/app/runtime/sdk-rtsp/www/cgi-bin/live.page",
                         "/app/runtime/sdk-rtsp/www/a/anyka-preview.js", "/app/runtime/sdk-rtsp/www/a/video-rtc.js",
                         "/app/tools/usr/bin/haserl", "/app/tools/usr/bin/curl"]:
                assert resolve(name).is_file(), name
            for p in (root / "app/runtime/sdk-rtsp/www").rglob("*"):
                if p.is_file() and (p.suffix in {".cgi", ".sh"} or p.name == "openipc-sh"):
                    assert p.stat().st_mode & 0o111, str(p)
            for name in ["go2rtc", "go2rtc.yaml", "go2rtc.json", "anyka-audio-play"]:
                assert not resolve("/app/runtime/sdk-rtsp/" + name).exists(), name
            assert digest(resolve("/app/runtime/sdk-rtsp/anyka-gateway").read_bytes()) == report["gateway_sha256"]
            assert 'ANYKA_WEB_UI=1' in resolve("/app/runtime/sdk-rtsp/start-web.sh").read_text()
            assert 'anyka-gateway' in resolve("/etc/init.d/internal-monitor.sh").read_text()
            for p in (root / "app/tools/usr/lib").iterdir():
                if not p.is_file() and not p.is_symlink(): continue
                data = subprocess.check_output(["readelf", "-d", str(resolve("/app/tools/usr/lib/" + p.name))], text=True)
                for name in re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", data):
                    assert resolve("/app/tools/usr/lib/" + name).is_file() or resolve("/lib/" + name).is_file(), name
        qemu = ["qemu-arm-static", "-cpu", "arm926", "-L", str(root)]
        version = subprocess.check_output(qemu + [str(resolve("/app/runtime/sdk-rtsp/" + gateway_name)), "-version"], text=True).strip()
        for name in ["/etc/init.d/rcS", "/etc/init.d/internal-monitor.sh", "/etc/init.d/internal-check-view.sh",
                     "/app/ak/load.sh", "/app/runtime/sdk-rtsp/start.sh", "/app/runtime/sdk-rtsp/start-web.sh"]:
            subprocess.run(qemu + [str(root / "bin/busybox"), "sh", "-n", str(resolve(name))], check=True)
        sdk = Path(__file__).resolve().parent.parent / "sd-openipc-test/openipc-persist/sdk-rtsp"
        stripped = {entry['file'] for entry in report.get('runtime_symbol_stripping', [])}
        def unchanged(name):
            actual = resolve('/app/runtime/sdk-rtsp/' + name).read_bytes()
            original = (sdk / name).read_bytes()
            if name in stripped:
                assert_same_runtime(original, actual)
            else:
                assert actual == original, name
        library_count = 0
        for p in (sdk / "lib").iterdir():
            unchanged('lib/' + p.name)
            library_count += 1
        for name in ["ak_rtsp_server_sample", "ak_venc_adapter.ko", "ak_venc_bridge.ko", "ak_npu.ko",
                     "anyka-daynight.so", "anyka-native-rtsp.so", "anyka-rtsp-auth.so",
                     "models/model_0x0A000000_V1.1.0.bin"]:
            unchanged(name)
        result = {"partition_offsets_and_padding": "passed", "image_manifests": "passed",
                  "early_console_and_null_device_nodes": "passed",
                  "manifest_file_checks": checked, "sdk_libraries_with_identical_runtime_contents": library_count,
                  "sdk_sample_modules_interposers_model_runtime_contents": "passed",
                  "files_with_non_runtime_symbols_removed": len(stripped),
                  "arm926_gateway_execution": version, "arm926_shell_syntax": "passed",
                  "hardware_boot_validated": False, "private_configuration_provisioned": False}
        if report.get("headless"):
            result["ui_assets_and_legacy_relay_configuration_removed"] = "passed"
            result["retained_backend_cgi_files"] = len(backend)
        elif report.get("gateway_implementation") == "C":
            result["openipc_pages_and_native_preview_assets"] = "passed"
            result["go_gateway_and_relay_removed"] = "passed"
            result["https_helper_dependencies"] = "passed"
        (out / "VERIFICATION.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
