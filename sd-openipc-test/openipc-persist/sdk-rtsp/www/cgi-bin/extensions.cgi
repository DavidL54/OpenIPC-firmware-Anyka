#!/bin/sh
case "${REQUEST_METHOD:-GET}" in GET|HEAD) ;; *) exit 1 ;; esac
case "$0" in */*) cd -- "${0%/*}" || exit 1 ;; esac
export OPENIPC_ANYKA_WEBUI=1 LD_LIBRARY_PATH=/media/lib:/media/usr/lib
exec /media/lib/ld-musl-arm.so.1 /media/usr/bin/haserl -s "$PWD/p/openipc-sh" extensions.page