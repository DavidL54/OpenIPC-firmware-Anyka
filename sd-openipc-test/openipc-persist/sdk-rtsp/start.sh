#!/bin/sh
set -eu

SCRIPT_PATH=$0
case "$SCRIPT_PATH" in
	*/*) SDK_DIR=${SCRIPT_PATH%/*} ;;
	*) SDK_DIR=. ;;
esac
SDK_DIR=$(CDPATH= cd -- "$SDK_DIR" && pwd)

bb() {
	/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
}
bb ifconfig lo 127.0.0.1 up
[ -r "$SDK_DIR/anyka-rtsp-auth.so" ] || { echo 'SDK RTSP authentication module missing' >&2; exit 1; }
expected=$(bb awk -v target="$SDK_DIR/anyka-rtsp-auth.so" '$2 == "./anyka-rtsp-auth.so" { print $1 "  " target }' "$SDK_DIR/SHA256SUMS")
[ -n "$expected" ] || { echo 'SDK RTSP authentication checksum missing' >&2; exit 1; }
bb printf '%s\n' "$expected" | bb sha256sum -c -
PRIVATE_DIR=/conf/anyka-private
[ ! -L "$PRIVATE_DIR" ] || { echo 'Unsafe private runtime directory' >&2; exit 1; }
bb mkdir -p "$PRIVATE_DIR"
bb chmod 700 "$PRIVATE_DIR"

if [ ! -d /sys/module/sensor_sc3336 ]; then
	insmod /app/ak/modules/sensor_sc3336.ko SENSOR_ID=0x2b3a
elif [ "$(cat /sys/module/sensor_sc3336/parameters/SENSOR_ID)" != 11066 ]; then
	echo 'SDK RTSP: sensor already loaded with a different ID; restart with SENSOR_ID=0x2b3a' >&2
	exit 1
fi

rmmod ak_venc_bridge 2>/dev/null || true
rmmod ak_venc_adapter 2>/dev/null || true
insmod "$SDK_DIR/ak_venc_adapter.ko"
insmod "$SDK_DIR/ak_venc_bridge.ko"

export LD_LIBRARY_PATH="$SDK_DIR/lib"
export ANYKA_GPIO_HELPER="$SDK_DIR/anyka-gpio"
export ANYKA_DAYNIGHT_CONTROL="${SDK_DAYNIGHT_CONTROL:-1}"
export ANYKA_OSD_CONFIG="$SDK_DIR/osd-clock.conf"
export ANYKA_MOTION_DIR="$SDK_DIR"
export ANYKA_PTZ_HELPER="$SDK_DIR/anyka-ptz"
export ANYKA_NPU_HELPER="$SDK_DIR/prepare-npu.sh"
export TZ='CET-1CEST,M3.5.0,M10.5.0/3'
export ANYKA_NATIVE_RTSP="${ANYKA_NATIVE_RTSP:-1}"
NATIVE_PRELOAD=
if [ "$ANYKA_NATIVE_RTSP" = 1 ]; then
	[ -r "$SDK_DIR/anyka-native-rtsp.so" ] || { echo 'Native SDK RTSP module missing' >&2; exit 1; }
	expected=$(bb awk -v target="$SDK_DIR/anyka-native-rtsp.so" '$2 == "./anyka-native-rtsp.so" { print $1 "  " target }' "$SDK_DIR/SHA256SUMS")
	[ -n "$expected" ] || { echo 'Native SDK RTSP checksum missing' >&2; exit 1; }
	bb printf '%s\n' "$expected" | bb sha256sum -c -
	NATIVE_PRELOAD=":$SDK_DIR/anyka-native-rtsp.so"
fi
if [ "${SDK_DISABLE_AUDIO:-0}" = 1 ]; then
	export LD_PRELOAD="$SDK_DIR/anyka-rtsp-auth.so$NATIVE_PRELOAD:$SDK_DIR/disable-audio.so:$SDK_DIR/anyka-daynight.so"
else
	export LD_PRELOAD="$SDK_DIR/anyka-rtsp-auth.so$NATIVE_PRELOAD:$SDK_DIR/anyka-daynight.so"
fi
exec "$SDK_DIR/ak_rtsp_server_sample" -i "$SDK_DIR/rtsp.ini"
