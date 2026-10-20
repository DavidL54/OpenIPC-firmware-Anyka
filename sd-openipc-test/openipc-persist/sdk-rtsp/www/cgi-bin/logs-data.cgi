#!/bin/sh
case "$0" in */*) cd -- "${0%/*}" || exit 1 ;; esac
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox sh p/logs.sh