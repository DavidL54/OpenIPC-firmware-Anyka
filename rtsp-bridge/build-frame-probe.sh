#!/bin/sh
set -eu

SCRIPT_PATH=$0
case "$SCRIPT_PATH" in
	*/*) SCRIPT_DIR=${SCRIPT_PATH%/*} ;;
	*) SCRIPT_DIR=. ;;
esac
SCRIPT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR" && pwd)
REPOSITORY_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TOOLCHAIN_DIR="$REPOSITORY_DIR/openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin"
CC="$TOOLCHAIN_DIR/arm-linux-gcc"
READELF="$TOOLCHAIN_DIR/arm-linux-readelf"
OUTPUT="$SCRIPT_DIR/frame-probe.so"

"$CC" \
	-shared -fPIC -fno-stack-protector -nostdlib \
	-Wl,--hash-style=sysv \
	-o "$OUTPUT" "$SCRIPT_DIR/frame-probe.c"

"$READELF" -h "$OUTPUT"
"$READELF" -d "$OUTPUT"