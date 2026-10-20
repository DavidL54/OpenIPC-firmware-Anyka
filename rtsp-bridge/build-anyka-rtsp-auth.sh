#!/bin/sh
set -eu
case "$0" in */*) SCRIPT_DIR=${0%/*} ;; *) SCRIPT_DIR=. ;; esac
SCRIPT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR" && pwd)
REPOSITORY_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TOOLCHAIN_DIR="$REPOSITORY_DIR/openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin"
SDK_INCLUDE="$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform/sample/rtsp"
SDK_PLATFORM_INCLUDE="$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform/include"
OUTPUT="$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/anyka-rtsp-auth.so"
"$TOOLCHAIN_DIR/arm-linux-gcc" -shared -fPIC -Os -Wall -Wextra -Werror \
    -march=armv5te -fno-stack-protector -nostdlib -Wl,--hash-style=sysv \
    -I"$SDK_INCLUDE" -I"$SDK_PLATFORM_INCLUDE" -o "$OUTPUT" "$SCRIPT_DIR/anyka-rtsp-auth.c"
"$TOOLCHAIN_DIR/arm-linux-readelf" -h -d "$OUTPUT"