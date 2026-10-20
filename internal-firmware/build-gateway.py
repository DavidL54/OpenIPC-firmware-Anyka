#!/usr/bin/env python3
"""Build the internal profile without modifying the validated SD bundle."""
import argparse
import hashlib
import json
from headless import prepare as prepare_headless
import os
from pathlib import Path
import shutil
import subprocess

BASE = Path(__file__).resolve().parent.parent
FILES = ["go2rtc-anyka.go", "anyka-runtime-status.go", "anyka-metrics.go",
         "anyka-pages.go", "anyka-native-auth.go", "anyka-native-audio.go"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--headless", action="store_true", help="Native RTSP and authenticated API only; remove browser UI/media relay")
    parser.add_argument("--compact", action="store_true", help="Disable inlining to reduce flash footprint")
    args = parser.parse_args()
    if args.headless and args.check:
        parser.error("--check uses the legacy GUI unit suites; use verify-images.py and camera validation for the headless profile")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    work = output / "gateway-source"
    if work.exists():
        raise SystemExit("Existing gateway source preserved; choose a fresh output directory")
    template = BASE / "recovery-build/internal-migration-20261015/go2rtc-source"
    shutil.copytree(template, work)
    files = FILES
    if args.headless:
        audit = prepare_headless(work)
        (output / "PROFILE.json").write_text(json.dumps(audit, indent=2) + "\n")
        files = [f for f in FILES if f != "anyka-pages.go"] + ["headless-routes.go"]
    else:
        shutil.copy2(BASE / "internal-firmware/gateway-config.go", work / "internal/app/config.go")
        shutil.copy2(BASE / "internal-firmware/gateway-config_test.go", work / "internal/app/internal_config_test.go")
        (work / "internal/app/storage.go").write_text("package app\n")
        shutil.copy2(BASE / "internal-firmware/gateway-log.go", work / "internal/app/log.go")
        (work / "internal/api/config.go").write_text(
            'package api\nimport "net/http"\n'
            'func configHandler(w http.ResponseWriter, r *http.Request) { http.NotFound(w, r) }\n')
        (work / "www/static.go").write_text('package www\nimport "embed"\n'
            '//go:embed index.html stream.html video-rtc.js video-stream.js\nvar Static embed.FS\n')
        (work / "www/index.html").write_text('<!doctype html><meta charset="utf-8">'
            '<title>Camera preview</title><a href="stream.html?src=preview&amp;mode=mse">Preview</a>\n')
        # These handlers already return 404 under the camera allowlist. Excluding
        # their registration lets the linker remove unused admin-only code.
        for relative, handlers in {
            "internal/api/api.go": ["api", "api/config", "api/exit", "api/restart", "api/log"],
            "internal/streams/streams.go": ["api/streams", "api/streams.dot", "api/preload", "api/schemes"],
        }.items():
            p = work / relative
            lines = p.read_text().splitlines(keepends=True)
            lines = [line for line in lines if not any('HandleFunc("' + h + '"' in line for h in handlers)]
            text = ''.join(lines)
            if relative == "internal/streams/streams.go":
                text = text.replace('\t"github.com/AlexxIT/go2rtc/internal/api"\n', '')
            p.write_text(text)
        # Native public RTSP still needs the RTSP *client* for browser MSE. Register
        # that client in native/UI mode without opening a second listener on :554.
        main_path = work / "go2rtc-anyka.go"
        text = main_path.read_text()
        old = '\tif !native {\n\t\trtsp.Init()\n\t}'
        if text.count(old) != 1:
            raise SystemExit("Unexpected gateway entry point")
        main_path.write_text(text.replace(old, '\tif ui || !native {\n\t\trtsp.Init()\n\t}'))
        rtsp_path = work / "internal/rtsp/rtsp.go"
        text = rtsp_path.read_text().replace('\t"net/url"', '\t"net/url"\n\t"os"')
        old = '\taddress := conf.Mod.Listen\n'
        if text.count(old) != 1:
            raise SystemExit("Unexpected RTSP initializer")
        rtsp_path.write_text(text.replace(old, old + '\tif os.Getenv("ANYKA_NATIVE_RTSP") == "1" { address = "" }\n'))
        shutil.copy2(BASE / "internal-firmware/gateway-preview_test.go", work / "internal/rtsp/internal_preview_test.go")
    go = os.environ.get("GO_BIN", "/usr/local/go/bin/go")
    env = dict(os.environ, GOCACHE="/tmp/anyka-internal-go-cache", GOPROXY="off",
               GOSUMDB="off", CGO_ENABLED="0")
    def run(items):
        subprocess.run([go] + items, cwd=work, env=env, check=True)
    if args.check:
        run(["test", "-mod=mod", "-gcflags=all=-l", "./internal/app"])
        run(["test", "-mod=mod", "-gcflags=all=-l", "./internal/rtsp", "-run", "TestInternal", "-count=1"])
        run(["test", "-mod=mod", "-gcflags=all=-l"] + files + [
            "anyka-native-auth_test.go", "anyka-native-audio_test.go",
            "anyka-pages_test.go", "anyka-gateway_test.go"])
        run(["test", "-mod=mod", "-gcflags=all=-l", "./pkg/rtsp", "-run", "TestAnyka", "-count=1"])
    env.update(GOOS="linux", GOARCH="arm", GOARM="5")
    binary = output / "go2rtc-internal-armv5"
    flags = ["-tags=nethttpomithttp2"] if args.headless else ["-gcflags=all=-l"]
    if args.headless and args.compact: flags += ["-gcflags=all=-l"]
    run(["build", "-mod=mod", "-trimpath"] + flags + ["-ldflags=-s -w", "-o", str(binary)] + files)
    if args.headless:
        deps = subprocess.check_output([go, "list", "-mod=mod", "-deps"] + flags + files, cwd=work, env=env, text=True).splitlines()
        forbidden = ["github.com/AlexxIT/go2rtc/" + n for n in ["internal/api", "internal/app", "internal/mp4", "internal/streams", "internal/rtsp", "pkg/rtsp", "pkg/core", "www"]]
        assert not set(deps).intersection(forbidden), "UI/media relay still linked"
        (output / "LINKED-PACKAGES.txt").write_text("\n".join(deps) + "\n")
    if args.headless:
        audit["gateway_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        audit["gateway_bytes"] = binary.stat().st_size
        audit["build_flags"] = flags
        (output / "PROFILE.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(f"Gateway: {binary.stat().st_size} bytes; SHA256 {hashlib.sha256(binary.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
