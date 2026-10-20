#!/bin/sh
reply_error() {
    printf 'Status: %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n{"ok":false,"error":"%s"}\n' "$1" "$2"
    exit 0
}
case "${REQUEST_METHOD:-GET}:$QUERY_STRING" in
    GET:action=status) action=status ;;
    GET:action=mode-status) action=mode-status ;;
    GET:action=daynight-status)
        [ -r /tmp/anyka-daynight.json ] || reply_error '503 Service Unavailable' 'Day/night controller unavailable'
        printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
        cat /tmp/anyka-daynight.json
        exit 0
        ;;
    POST:*)
        case "$HTTP_HOST" in
            ''|*[!a-zA-Z0-9.:-]*) reply_error '403 Forbidden' 'Invalid host' ;;
        esac
        [ "$HTTP_ORIGIN" = "http://$HTTP_HOST" ] || reply_error '403 Forbidden' 'Same-origin request required'
        case "$QUERY_STRING" in
            action=ir-on|action=ir-off|action=blue-on|action=blue-off|action=mode-auto|action=mode-day|action=mode-night|action=mode-manual) action=${QUERY_STRING#action=} ;;
            *) reply_error '400 Bad Request' 'Invalid GPIO action' ;;
        esac
        ;;
    *) reply_error '405 Method Not Allowed' 'POST required for changes' ;;
esac
result=$(/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib ../../anyka-gpio "$action" 2>/tmp/gpio-error.log) || reply_error '503 Service Unavailable' 'GPIO unavailable or command rejected'
printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s\n' "$result"