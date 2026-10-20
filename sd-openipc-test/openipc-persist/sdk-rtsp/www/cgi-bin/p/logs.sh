#!/bin/sh
case "${REQUEST_METHOD:-GET}" in
 GET|HEAD) ;;
 *) printf 'Status: 405 Method Not Allowed\r\nAllow: GET, HEAD\r\n\r\n'; exit ;;
esac
source=${QUERY_STRING:-source=all}
case "$source" in
 source=all|source=sdk|source=relay|source=kernel|source=ptz) source=${source#source=} ;;
 *) printf 'Status: 400 Bad Request\r\n\r\n'; exit ;;
esac
printf 'Content-Type: text/plain; charset=UTF-8\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n'
[ "${REQUEST_METHOD:-GET}" = HEAD ] && exit
log_file() {
 [ -r "$2" ] && tail -n 120 "$2" | sed "s/^/[$1] /"
}
case "$source" in all|sdk) log_file SDK /tmp/sdk-rtsp.log.1; log_file SDK /tmp/sdk-rtsp.log ;; esac
case "$source" in all|relay) log_file Relay /tmp/go2rtc.log ;; esac
case "$source" in all|ptz) log_file PTZ /tmp/ptz-init.log; log_file PTZ /tmp/ptz-error.log ;; esac
case "$source" in all|kernel) dmesg | tail -n 120 | sed 's/^/[Kernel] /' ;; esac
