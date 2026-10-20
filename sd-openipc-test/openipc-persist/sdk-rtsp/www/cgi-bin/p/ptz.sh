#!/bin/sh
reply_error() {
    printf 'Status: %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n{"ok":false,"error":"%s"}\n' "$1" "$2"
    exit 0
}
case "${REQUEST_METHOD:-GET}" in
    POST) ;;
    *) reply_error '405 Method Not Allowed' 'POST required' ;;
esac
case "$HTTP_HOST" in
    ''|*[!a-zA-Z0-9.:-]*) reply_error '403 Forbidden' 'Invalid host' ;;
esac
[ "$HTTP_ORIGIN" = "http://$HTTP_HOST" ] || reply_error '403 Forbidden' 'Same-origin request required'
case "$QUERY_STRING" in
    action=left|action=right|action=up|action=down|action=stop) action=${QUERY_STRING#action=} ;;
    *) reply_error '400 Bad Request' 'Invalid PTZ action' ;;
esac
result=$(/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib ../../anyka-ptz "$action" 2>/tmp/ptz-error.log)
result_code=$?
case "$result_code" in
    0) ;;
    3) reply_error '409 Conflict' 'Travel limit reached; use the opposite direction' ;;
    4) reply_error '503 Service Unavailable' 'Motor calibration required; check the startup homing log' ;;
    *) reply_error '503 Service Unavailable' 'Motor unavailable or movement rejected' ;;
esac
printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s\n' "$result"