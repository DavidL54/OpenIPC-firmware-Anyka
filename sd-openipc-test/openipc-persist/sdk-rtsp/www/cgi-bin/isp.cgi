#!/bin/sh
SCRIPT_DIR=${0%/*}
[ "$SCRIPT_DIR" != "$0" ] || SCRIPT_DIR=.
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox sh "$SCRIPT_DIR/p/image.sh"