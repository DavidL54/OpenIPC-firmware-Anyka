#!/bin/sh
set -eu
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
unset LD_PRELOAD LD_LIBRARY_PATH
export ANYKA_NATIVE_RTSP=${ANYKA_NATIVE_RTSP:-1}
bb() { /bin/busybox "$@"; }
SDK=/mnt/openipc-persist/sdk-rtsp
recovery_attempts=0

failed() {
    echo "internal-boot: $1; no automatic reboot; inspect via SSH/UART" >&2
    echo "$1" > /tmp/anyka-internal-failed
    exit 1
}
if bb pidof ak_rtsp_server_sample go2rtc >/dev/null 2>&1; then
    failed 'services already running; refusing a second launch'
fi
sdk_healthy() {
    bb pidof ak_rtsp_server_sample >/dev/null 2>&1 || return 1
    bb awk -v native="$ANYKA_NATIVE_RTSP" '$4 == "0A" {
        if ($2 ~ /:022A$/) rtsp=1
        if ($2 == "0100007F:216B") sdk=1
    } END { exit !(sdk && (native != 1 || rtsp)) }' /proc/net/tcp || return 1
    bb grep -q 'Video encoder open success' /tmp/sdk-rtsp.log || return 1
    [ "$ANYKA_NATIVE_RTSP" != 1 ] || [ -S /conf/anyka-private/native-rtsp-auth.sock ]
}
healthy() {
    sdk_healthy || return 1
    bb pidof go2rtc >/dev/null 2>&1 || return 1
    bb awk '$4 == "0A" {
        if ($2 ~ /:0050$/) web=1
        if ($2 ~ /:022A$/) rtsp=1
        if ($2 == "0100007F:1F90") cgi=1
    } END { exit !(web && rtsp && cgi) }' /proc/net/tcp || return 1
    bb awk '$1 == "wlan0" && $2 == "00000000" && $3 != "00000000" { found=1 }
        END { exit !found }' /proc/net/route
}
start_sdk() {
    /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib \
        "$SDK/anyka-sdk-log" /tmp/sdk-rtsp.log "$SDK/start.sh" &
    sdk_logger=$!
}
recovery_status() {
    # RAM only: never write a failure/retry counter into NOR.
    printf 'attempts=%s limit=3 state=%s\n' "$recovery_attempts" "$1" > /tmp/anyka-internal-recovery.tmp
    bb mv /tmp/anyka-internal-recovery.tmp /tmp/anyka-internal-recovery
}
recover_sdk() {
    while [ "$recovery_attempts" -lt 3 ]; do
        recovery_attempts=$((recovery_attempts + 1))
        recovery_status stopping
        # Never overlap encoder instances, force-unload modules, reboot or
        # invoke start-web.sh here (it also performs startup PTZ homing).
        for pid in $(bb pidof ak_rtsp_server_sample || :); do bb kill -TERM "$pid" 2>/dev/null || :; done
        elapsed=0
        while bb pidof ak_rtsp_server_sample >/dev/null 2>&1 && [ "$elapsed" -lt 20 ]; do
            bb sleep 1; elapsed=$((elapsed + 1))
        done
        if bb pidof ak_rtsp_server_sample >/dev/null 2>&1; then
            recovery_status stop-failed
            failed 'video service did not stop gracefully; recovery suspended'
        fi
        # Reap only our previous bounded logger; it must have closed its pipes
        # before a new writer uses the same bounded log files.
        elapsed=0
        while bb kill -0 "$sdk_logger" 2>/dev/null && [ "$elapsed" -lt 5 ]; do
            bb sleep 1; elapsed=$((elapsed + 1))
        done
        if bb kill -0 "$sdk_logger" 2>/dev/null; then
            recovery_status logger-failed
            failed 'video logger did not exit; recovery suspended'
        fi
        wait "$sdk_logger" 2>/dev/null || :
        case "$recovery_attempts" in 1) delay=10 ;; 2) delay=30 ;; *) delay=60 ;; esac
        recovery_status waiting
        bb sleep "$delay"
        recovery_status starting
        start_sdk
        elapsed=0
        while [ "$elapsed" -lt 90 ]; do
            if sdk_healthy; then
                recovery_status recovered
                echo "internal-boot: video recovered (attempt $recovery_attempts/3)"
                return 0
            fi
            bb sleep 3; elapsed=$((elapsed + 3))
        done
    done
    recovery_status exhausted
    failed 'video recovery limit reached (three attempts per boot); SSH remains available'
}

start_sdk
bb sleep 10
/bin/sh "$SDK/start-web.sh" >/tmp/internal-web-start.log 2>&1 &
launcher=$!
elapsed=0
while [ "$elapsed" -lt 240 ]; do
    if healthy; then break; fi
    if [ -n "$launcher" ] && ! bb kill -0 "$launcher" 2>/dev/null; then
        wait "$launcher" || failed 'web launcher failed'
        launcher=
    fi
    bb sleep 5
    elapsed=$((elapsed + 5))
done
if ! healthy; then
    if ! sdk_healthy; then recover_sdk; fi
    healthy || failed 'non-video service unavailable after startup; inspect via SSH'
fi
recovery_status healthy
echo 'internal-boot: services ready'
failures=0
while :; do
    bb sleep 5
    if healthy; then failures=0; else failures=$((failures + 1)); fi
    if [ "$failures" -ge 3 ]; then
        if sdk_healthy; then
            failed 'network or web service unhealthy; no video restart required'
        fi
        recover_sdk
        failures=0
    fi
done
