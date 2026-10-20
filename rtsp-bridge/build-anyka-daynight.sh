#!/bin/sh
set -eu
case "$0" in */*) SCRIPT_DIR=${0%/*} ;; *) SCRIPT_DIR=. ;; esac
SCRIPT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR" && pwd)
REPOSITORY_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TOOLCHAIN_DIR="$REPOSITORY_DIR/openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin"
SDK_INCLUDE="$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform/include"
OUTPUT="$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/anyka-daynight.so"
MT_SOURCE="$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform/sample/mt"
MT_OBJECT=$(mktemp)
trap 'rm -f "$MT_OBJECT"' EXIT
"$TOOLCHAIN_DIR/arm-linux-gcc" -shared -fPIC -Os -march=armv5te -nostdlib \
    -Wl,--hash-style=sysv -Wl,--no-as-needed -L"$SCRIPT_DIR/../AnyCloud39AV130_SDK_V1.03/platform/lib" \
    -x c /dev/null -x none -lplat_npu -lak_nne -lmpi_svp2 \
    -o "$SCRIPT_DIR/../sd-openipc-test/openipc-persist/sdk-rtsp/lib/anyka-person-sdk.so"
"$TOOLCHAIN_DIR/arm-linux-gcc" -c -fPIC -Os -w -march=armv5te -fno-stack-protector \
    -I"$MT_SOURCE" -o "$MT_OBJECT" "$MT_SOURCE/ak_mt.c"
"$TOOLCHAIN_DIR/arm-linux-gcc" -shared -fPIC -Os -Wall -Wextra -Werror \
	-march=armv5te -fno-stack-protector -nostdlib -Wl,--hash-style=sysv \
	-D__CHIP_AK3918AV130_SERIES -isystem "$SDK_INCLUDE" -I"$MT_SOURCE" \
	-o "$OUTPUT" "$SCRIPT_DIR/anyka-daynight.c" "$MT_OBJECT" -lgcc
"$TOOLCHAIN_DIR/arm-linux-readelf" -h -d "$OUTPUT"
