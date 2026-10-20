#!/bin/sh
set -eu
case "$0" in */*) SDK_DIR=${0%/*} ;; *) SDK_DIR=. ;; esac
SDK_DIR=$(CDPATH= cd -- "$SDK_DIR" && pwd)
MUSL_LD=/media/lib/ld-musl-arm.so.1
MUSL_LIBS=/media/lib:/media/usr/lib
BUSYBOX=/media/bin/busybox
# Per-camera preference, excluded from public bundles. An environment override
# permits a temporary UI session without changing the persisted preference.
if [ -z "${ANYKA_WEB_UI:-}" ] && [ -f "$SDK_DIR/../web-ui.conf" ]; then
    IFS= read -r ANYKA_WEB_UI < "$SDK_DIR/../web-ui.conf" || :
fi
export ANYKA_WEB_UI=${ANYKA_WEB_UI:-1}
case "$ANYKA_WEB_UI" in 0|1) ;; *) echo 'Invalid web-ui.conf (expected 0 or 1)' >&2; exit 1 ;; esac
$MUSL_LD --library-path "$MUSL_LIBS" "$BUSYBOX" ifconfig lo 127.0.0.1 up
if [ ! -c /dev/gv-motor ]; then
    insmod /app/drivers/gv_step_motor.ko
fi
if [ -f /tmp/anyka-ptz-homed ] &&
    $MUSL_LD --library-path "$MUSL_LIBS" "$SDK_DIR/anyka-ptz" status >/tmp/ptz-init.log 2>&1; then
    :
else
    rm -f /tmp/anyka-ptz-homed
    $MUSL_LD --library-path "$MUSL_LIBS" "$SDK_DIR/anyka-ptz" home >/tmp/ptz-init.log 2>&1 ||
        echo 'PTZ homing failed; movements remain disabled' >&2
fi
$MUSL_LD --library-path "$MUSL_LIBS" "$BUSYBOX" sh -c '
    echo 1000 > /proc/$$/oom_score_adj
    export GOMEMLIMIT=6MiB GOGC=100 GOMAXPROCS=1
    export ANYKA_NATIVE_RTSP="${ANYKA_NATIVE_RTSP:-1}"
    export ANYKA_ACCESS_FILE="$1/../web-access.json"
    export SSL_CERT_FILE=/media/etc/ssl/certs/ca-certificates.crt
    exec "$1/go2rtc" -config "$1/go2rtc.yaml"
' sh "$SDK_DIR" >/tmp/go2rtc.log 2>&1 &
$MUSL_LD --library-path "$MUSL_LIBS" "$BUSYBOX" httpd -p 127.0.0.1:8080 -h "$SDK_DIR/www"

# Native C images replace this marker with the startup-position hook.
# ANYKA_PTZ_STARTUP_HOOK
