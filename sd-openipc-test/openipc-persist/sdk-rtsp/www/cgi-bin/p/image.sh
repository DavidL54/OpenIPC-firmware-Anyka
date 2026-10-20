#!/bin/sh
set -eu
bb() {
    /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
}
reply_error() {
    printf 'Status: %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n{"ok":false,"error":"%s"}\n' "$1" "$2"
    exit 0
}
snapshot=0
case "${SCRIPT_NAME##*/}" in
    snapshot.cgi) snapshot=1 ;;
    isp.cgi) ;;
    *) reply_error '404 Not Found' 'Unknown image operation' ;;
esac
case "${REQUEST_METHOD:-GET}" in
    GET)
        [ -z "${QUERY_STRING:-}" ] || reply_error '400 Bad Request' 'Unexpected query'
        if [ "$snapshot" -eq 0 ]; then
            [ -r /tmp/anyka-image.json ] || reply_error '503 Service Unavailable' 'Image settings unavailable'
            read -r uptime rest < /proc/uptime
            now=${uptime%%.*}
            modified=$(bb stat -c %Y /tmp/anyka-image.json)
            age=$(( $(bb date +%s) - modified ))
            [ "$age" -ge 0 ] && [ "$age" -le 10 ] || reply_error '503 Service Unavailable' 'Image settings stale'
            printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
            bb cat /tmp/anyka-image.json
            exit 0
        fi
        operation=snapshot
        value=0
        ;;
    POST)
        [ "$snapshot" -eq 0 ] || reply_error '405 Method Not Allowed' 'Use GET for snapshots'
        case "${HTTP_HOST:-}" in
            ''|*[!a-zA-Z0-9.:-]*) reply_error '403 Forbidden' 'Invalid host' ;;
        esac
        [ "${HTTP_ORIGIN:-}" = "http://$HTTP_HOST" ] || reply_error '403 Forbidden' 'Same-origin request required'
        case "${QUERY_STRING:-}" in
            brightness=*|contrast=*|saturation=*|sharpness=*|hue=*|flip_vertical=*|flip_horizontal=*|osd_clock=*|motion_enabled=*|motion_sensitivity=*|motion_zone=*|privacy_mask1=*|privacy_mask2=*|privacy_mask3=*|privacy_mask4=*|privacy_style=*|wdr=*|power_hz=*|anti_flicker=*|exposure=*|white_balance_auto=*|white_balance_red=*|white_balance_green=*|white_balance_blue=*|sound_enabled=*|sound_sensitivity=*|tracking_enabled=*|person_enabled=*) ;;
            *) reply_error '400 Bad Request' 'Invalid image setting' ;;
        esac
        operation=${QUERY_STRING%%=*}
        value=$(bb httpd -d "${QUERY_STRING#*=}")
        [ "${#value}" -le 32 ] || reply_error '400 Bad Request' 'Value too long'
        case "$value" in ''|*[!0-9,-]*) reply_error '400 Bad Request' 'Invalid value' ;; esac
        case "$operation" in
            motion_zone|privacy_mask1|privacy_mask2|privacy_mask3|privacy_mask4)
                old_ifs=$IFS; IFS=,; set -- $value; IFS=$old_ifs
                [ "$#" -eq 4 ] || reply_error '400 Bad Request' 'Rectangle requires x,y,width,height'
                for component in "$@"; do
                    case "$component" in 0|[1-9]|[1-9][0-9]|100) ;; *) reply_error '400 Bad Request' 'Rectangle out of range' ;; esac
                done
                if [ "$value" != 0,0,0,0 ] || [ "$operation" = motion_zone ]; then
                    [ "$3" -gt 0 ] && [ "$4" -gt 0 ] && [ "$(( $1+$3 ))" -le 100 ] && [ "$(( $2+$4 ))" -le 100 ] || reply_error '400 Bad Request' 'Invalid rectangle'
                fi
                ;;
            *)
                case "$value" in -*) digits=${value#-} ;; *) digits=$value ;; esac
                case "$digits" in 0|[1-9]|[1-9][0-9]|[1-9][0-9][0-9]|1024) ;; *) reply_error '400 Bad Request' 'Noncanonical integer' ;; esac
                case "$operation" in
                    flip_vertical|flip_horizontal|osd_clock|motion_enabled|sound_enabled|tracking_enabled|person_enabled|privacy_style|white_balance_auto)
                        case "$value" in 0|1) ;; *) reply_error '400 Bad Request' 'Boolean required' ;; esac ;;
                    motion_sensitivity|sound_sensitivity) [ "$value" -ge 1 ] && [ "$value" -le 100 ] || reply_error '400 Bad Request' 'Sensitivity out of range' ;;
                    power_hz) case "$value" in 50|60) ;; *) reply_error '400 Bad Request' 'Frequency must be 50 or 60' ;; esac ;;
                    anti_flicker) [ "$value" -ge 0 ] && [ "$value" -le 100 ] || reply_error '400 Bad Request' 'Strength out of range' ;;
                    white_balance_red|white_balance_green|white_balance_blue) [ "$value" -ge 64 ] && [ "$value" -le 1024 ] || reply_error '400 Bad Request' 'Gain out of range' ;;
                    *) [ "$value" -ge -50 ] && [ "$value" -le 50 ] || reply_error '400 Bad Request' 'Value out of range' ;;
                esac
                ;;
        esac
        ;;
    *) reply_error '405 Method Not Allowed' 'Unsupported method' ;;
esac
exec 9>/tmp/anyka-image-api.lock
bb flock -n 9 || reply_error '409 Conflict' 'Image operation busy'
[ "$snapshot" -eq 0 ] || [ ! -e /tmp/anyka-privacy-mode ] || reply_error '503 Service Unavailable' 'Camera privacy mode enabled'
if [ "$snapshot" -eq 1 ] && [ -f /tmp/anyka-snapshot.jpg ]; then
    age=$(( $(bb date +%s) - $(bb stat -c %Y /tmp/anyka-snapshot.jpg) ))
    if [ "$age" -ge 0 ] && [ "$age" -le 5 ]; then
        [ ! -e /tmp/anyka-privacy-mode ] || reply_error '503 Service Unavailable' 'Camera privacy mode enabled'
        printf 'Content-Type: image/jpeg\r\nCache-Control: no-store\r\n\r\n'
        bb cat /tmp/anyka-snapshot.jpg
        exit 0
    fi
fi
read -r uptime rest < /proc/uptime
deadline=$(( ${uptime%%.*} + 8 ))
bb rm -f /tmp/anyka-image-response
printf '%s %s %s %s\n' "$$" "$operation" "$value" "$deadline" > /tmp/anyka-image-request.new
bb mv /tmp/anyka-image-request.new /tmp/anyka-image-request
for attempt in 1 2 3 4 5 6 7 8; do
    if [ -r /tmp/anyka-image-response ]; then
        read -r token result < /tmp/anyka-image-response
        if [ "$token" = "$$" ]; then
            [ "$result" = ok ] || reply_error '503 Service Unavailable' 'SDK rejected image operation'
            setting=
            case "$operation" in
                osd_clock) setting=../../osd-clock.conf ;;
                motion_enabled) setting=../../motion-enabled.conf ;;
                motion_sensitivity) setting=../../motion-sensitivity.conf ;;
                brightness|contrast|saturation|sharpness|hue|flip_vertical|flip_horizontal|motion_zone|privacy_mask1|privacy_mask2|privacy_mask3|privacy_mask4|privacy_style|wdr|power_hz|anti_flicker|exposure|white_balance_auto|white_balance_red|white_balance_green|white_balance_blue|sound_enabled|sound_sensitivity|tracking_enabled|person_enabled) setting=../../native-$operation.conf ;;
            esac
            if [ -n "$setting" ]; then
                # Only user changes write SD; no per-frame or per-second writes.
                [ ! -L "$setting" ] && [ ! -L "$setting.new" ] || reply_error '503 Service Unavailable' 'Unsafe camera setting'
                exec 8>/tmp/anyka-persist.lock
                bb flock 8 || reply_error '503 Service Unavailable' 'Camera persistence busy'
                bb mount -o remount,rw /mnt || reply_error '503 Service Unavailable' 'SD setting unavailable'
                trap 'bb sync; bb mount -o remount,ro /mnt' EXIT
                printf '%s\n' "$value" > "$setting.new" && bb mv "$setting.new" "$setting" || reply_error '503 Service Unavailable' 'Camera setting not saved'
                bb sync
                bb mount -o remount,ro /mnt || reply_error '503 Service Unavailable' 'SD read-only remount failed'
                trap - EXIT
            fi
            if [ "$snapshot" -eq 1 ]; then
                [ ! -e /tmp/anyka-privacy-mode ] || reply_error '503 Service Unavailable' 'Camera privacy mode enabled'
                [ -s /tmp/anyka-snapshot.jpg ] || reply_error '503 Service Unavailable' 'Snapshot unavailable'
                printf 'Content-Type: image/jpeg\r\nCache-Control: no-store\r\n\r\n'
                bb cat /tmp/anyka-snapshot.jpg
            else
                printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
                bb cat /tmp/anyka-image.json
            fi
            exit 0
        fi
    fi
    bb sleep 1
done
reply_error '504 Gateway Timeout' 'SDK image operation timed out'
