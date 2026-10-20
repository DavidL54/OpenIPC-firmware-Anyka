#!/bin/sh
# SDK NPU ABI 2.0.25, RAM only; retain the stock file and restore it on failure.
set -eu
case "$0" in */*) SDK_DIR=${0%/*} ;; *) SDK_DIR=. ;; esac
bb() { /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
if [ -r /sys/module/ak_npu/version ] && [ "$(cat /sys/module/ak_npu/version)" = 2.0.25 ]; then exit 0; fi
if [ -r /sys/module/ak_npu/version ] && [ "$(cat /sys/module/ak_npu/version)" != 2.0.08 ]; then exit 1; fi
expected=$(bb awk -v target="$SDK_DIR/ak_npu.ko" '$2 == "./ak_npu.ko" { print $1 "  " target }' "$SDK_DIR/SHA256SUMS")
[ -n "$expected" ] || exit 1
bb printf '%s\n' "$expected" | bb sha256sum -c -
if [ -d /sys/module/ak_npu ]; then rmmod ak_npu; fi
if insmod "$SDK_DIR/ak_npu.ko"; then
    [ "$(cat /sys/module/ak_npu/version)" = 2.0.25 ] && exit 0
    rmmod ak_npu
fi
insmod /app/ak/modules/ak_npu.ko || true
exit 1
