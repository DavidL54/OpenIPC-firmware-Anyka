#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUTPUT_DIR=${OUTPUT_DIR:-"$SCRIPT_DIR/../openipc-ak3918-build/output-toolchain-ak3918ev300"}
OUTPUT_DIR=$(CDPATH= cd -- "$OUTPUT_DIR" && pwd)
SOURCE_DIR="$OUTPUT_DIR/build/wpa_supplicant-2.10"
HOST_DIR="$OUTPUT_DIR/host"
STAGING_DIR="$OUTPUT_DIR/staging"
TARGET="$SCRIPT_DIR/build-openipc-sd/rootfs/usr/sbin/wpa_supplicant"
WORK_DIR=$(mktemp -d)

cleanup() {
	rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

if [ ! -f "$SOURCE_DIR/wpa_supplicant/.config" ]; then
	echo "Missing configured wpa_supplicant source: $SOURCE_DIR" >&2
	exit 1
fi

cp -a "$SOURCE_DIR/." "$WORK_DIR/"
patch -d "$WORK_DIR" -p1 < "$SCRIPT_DIR/patches/wpa_supplicant-2.10-anyka-realtime.patch"

make -C "$WORK_DIR/wpa_supplicant" clean
PATH="$HOST_DIR/bin:$PATH" \
CFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -Os -g0 -I$STAGING_DIR/usr/include/libnl3" \
make -C "$WORK_DIR/wpa_supplicant" "-j$(nproc)" \
	CC=arm-openipc-linux-musleabi-gcc BINDIR=/usr/sbin

"$HOST_DIR/bin/arm-openipc-linux-musleabi-strip" \
	-o "$TARGET" "$WORK_DIR/wpa_supplicant/wpa_supplicant"
chmod 0755 "$TARGET"
sha256sum "$TARGET"