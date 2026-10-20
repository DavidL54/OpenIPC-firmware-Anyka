#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GO_BIN=${GO_BIN:-/usr/local/go/bin/go}
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT HUP INT TERM
curl -fsSL https://github.com/AlexxIT/go2rtc/archive/refs/tags/v1.9.14.tar.gz | tar -xz --strip-components=1 -C "$WORK_DIR"
cp "$SCRIPT_DIR/go2rtc-anyka.go.in" "$WORK_DIR/go2rtc-anyka.go"
cp "$SCRIPT_DIR/anyka-runtime-status.go" "$WORK_DIR/anyka-runtime-status.go"
cp "$SCRIPT_DIR/anyka-metrics.go" "$WORK_DIR/anyka-metrics.go"
cp "$SCRIPT_DIR/anyka-pages.go" "$WORK_DIR/anyka-pages.go"
cp "$SCRIPT_DIR/anyka-pages_test.go" "$WORK_DIR/anyka-pages_test.go"
cp "$SCRIPT_DIR/anyka-gateway_test.go" "$WORK_DIR/anyka-gateway_test.go"
cp "$SCRIPT_DIR/anyka-native-audio_test.go" "$WORK_DIR/anyka-native-audio_test.go"
cp "$SCRIPT_DIR/anyka-native-audio.go" "$WORK_DIR/anyka-native-audio.go"
cp "$SCRIPT_DIR/anyka-native-auth.go" "$WORK_DIR/anyka-native-auth.go"
cp "$SCRIPT_DIR/anyka-native-auth_test.go" "$WORK_DIR/anyka-native-auth_test.go"
cd "$WORK_DIR"
patch -p1 < "$SCRIPT_DIR/go2rtc-origin.patch"
patch -p1 < "$SCRIPT_DIR/go2rtc-media-server.patch"
patch -p1 < "$SCRIPT_DIR/go2rtc-queue.patch"
patch -p1 < "$SCRIPT_DIR/go2rtc-bootstrap.patch"
patch -p1 < "$SCRIPT_DIR/go2rtc-parameters.patch"
cp "$SCRIPT_DIR/anyka-h264-parameters.go.in" pkg/core/anyka_h264_parameters.go
cp "$SCRIPT_DIR/anyka-bootstrap_test.go.in" pkg/rtsp/anyka_bootstrap_test.go
"$GO_BIN" get github.com/abbot/go-http-auth@v0.4.0
"$GO_BIN" test ./pkg/rtsp -run TestAnyka -count=1
"$GO_BIN" test go2rtc-anyka.go anyka-runtime-status.go anyka-metrics.go anyka-pages.go anyka-native-auth.go anyka-native-audio.go anyka-native-auth_test.go anyka-native-audio_test.go anyka-pages_test.go anyka-gateway_test.go
CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=5 "$GO_BIN" build -trimpath -ldflags='-s -w' -o "$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/go2rtc" go2rtc-anyka.go anyka-runtime-status.go anyka-metrics.go anyka-pages.go anyka-native-auth.go anyka-native-audio.go
"$GO_BIN" version -m "$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/go2rtc"
