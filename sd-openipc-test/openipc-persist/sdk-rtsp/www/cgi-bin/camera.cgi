#!/bin/sh
PAGE_DIR=${0%/*}
[ "$PAGE_DIR" != "$0" ] || PAGE_DIR=.
cd "$PAGE_DIR" || exit 1
export OPENIPC_ANYKA_WEBUI=1
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/usr/bin/haserl -s "$PWD/p/openipc-sh" camera.page