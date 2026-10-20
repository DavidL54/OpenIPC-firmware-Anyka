#!/bin/sh
case "${REQUEST_METHOD:-GET}" in
	GET|HEAD) ;;
	*) /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox printf 'Status: 405 Method Not Allowed\r\nAllow: GET, HEAD\r\n\r\n'; exit 0 ;;
esac
case "$0" in
	*/*) cd -- "${0%/*}" || exit 1 ;;
esac
export OPENIPC_ANYKA_WEBUI=1
export LD_LIBRARY_PATH=/media/lib:/media/usr/lib
exec /media/lib/ld-musl-arm.so.1 /media/usr/bin/haserl -s "$PWD/p/openipc-sh" streams.page