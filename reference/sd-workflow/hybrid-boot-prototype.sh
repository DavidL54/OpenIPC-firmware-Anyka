#!/bin/sh
set -eu

MODE=${1:---check}
case "$MODE" in
    --check|--run|--persistent) ;;
    *) echo 'Usage: /bin/busybox sh hybrid-boot-prototype.sh [--check|--run|--persistent]' >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || exit 2

SDK_DIR=/mnt/openipc-persist/sdk-rtsp
MUSL_LD=/media/lib/ld-musl-arm.so.1
MUSL_LIBS=/media/lib:/media/usr/lib
START_PID=

stock_bb() {
    /bin/busybox "$@"
}

bb() {
    "$MUSL_LD" --library-path "$MUSL_LIBS" /media/bin/busybox "$@"
}

fallback() {
    echo "hybrid-boot: stock requested: $1" >&2
    if [ "$MODE" != --check ]; then
        if [ -n "$START_PID" ]; then
            stock_bb kill "$START_PID" 2>/dev/null || true
        fi
        if [ "$MODE" = --persistent ]; then
            if stock_bb mount -o remount,rw /mnt; then
                if [ ! -L /mnt/boot-stock.flag ] &&
                    (umask 077; echo 'SD runtime failure' > /mnt/boot-stock.flag); then
                    stock_bb sync
                    if ! stock_bb mount -o remount,ro /mnt; then
                        echo 'hybrid-boot: cannot restore read-only SD; power off and remove SD' >&2
                        exit 21
                    fi
                else
                    stock_bb sync
                    stock_bb mount -o remount,ro /mnt || true
                    echo 'hybrid-boot: cannot persist stock selection; power off and remove SD' >&2
                    exit 21
                fi
            else
                echo 'hybrid-boot: SD not writable; power off and remove SD to avoid reboot loop' >&2
                exit 21
            fi
        fi
        stock_bb sync
        stock_bb reboot -f || true
        echo 'hybrid-boot: if reboot failed, power off, remove SD and power on' >&2
    fi
    exit 20
}

readonly_mount() {
    while IFS=' ' read -r device mountpoint filesystem options remainder; do
        if [ "$mountpoint" = "$1" ] && [ "$filesystem" = "$2" ]; then
            case ",$options," in
                *,ro,*) return 0 ;;
            esac
        fi
    done < /proc/mounts
    return 1
}

active_runtime() {
    for process_file in /proc/[0-9]*/comm; do
        [ -r "$process_file" ] || continue
        IFS= read -r process_name < "$process_file" || continue
        case "$process_name" in
            ipc|daemon|ak_rtsp_server*|go2rtc|wpa_supplicant) return 0 ;;
        esac
    done
    return 1
}

if [ "$MODE" != --check ]; then
    if [ "$MODE" = --persistent ]; then
        [ -f /etc/init.d/rc.local.stock ] &&
            [ -f /etc/init.d/hybrid-boot.sh ] || {
            echo 'hybrid-boot: persistent mode requires the candidate rootfs' >&2
            exit 2
        }
    else
    stock_bb grep -Eq '(^| )openipc_sdtest=1( |$)' /proc/cmdline &&
        stock_bb grep -Eq '(^| )init=/bin/sh( |$)' /proc/cmdline || {
        echo 'hybrid-boot: run refused outside the temporary init=/bin/sh SD boot' >&2
        exit 2
    }
    fi
    if active_runtime; then
        echo 'hybrid-boot: existing runtime detected; start from a fresh temporary boot' >&2
        exit 2
    fi
fi

readonly_mount / squashfs || fallback 'rootfs must be read-only'
readonly_mount /app squashfs || fallback 'stock application must be mounted read-only'
readonly_mount /mnt vfat || fallback 'SD FAT absent or not read-only'
[ ! -e /mnt/boot-stock.flag ] || fallback 'boot-stock.flag present'
readonly_mount /media squashfs || fallback 'SD userland absent or not read-only'
[ -x "$MUSL_LD" ] && [ -x /media/bin/busybox ] || fallback 'SD loader unavailable'
bb true || fallback 'SD userland cannot execute'
[ -f /mnt/stock-probe.sh ] && [ ! -L /mnt/stock-probe.sh ] || fallback 'SD launcher missing or unsafe'
[ -x "$SDK_DIR/start.sh" ] && [ -x "$SDK_DIR/start-web.sh" ] || fallback 'SDK launchers missing'
[ -s /mnt/openipc-persist/wpa_supplicant.conf ] &&
    [ ! -L /mnt/openipc-persist/wpa_supplicant.conf ] || fallback 'private Wi-Fi profile missing or unsafe'
for directory in /mnt "$SDK_DIR"; do
    [ -f "$directory/SHA256SUMS" ] && [ ! -L "$directory/SHA256SUMS" ] || fallback 'public manifest missing or unsafe'
    if bb grep -Eq '(^|/)(wpa_supplicant[^/]*|web-access\.json|[^/]*\.ha1)( |$)' "$directory/SHA256SUMS"; then
        fallback 'private file in public manifest'
    fi
    (cd "$directory" && bb sha256sum -c SHA256SUMS >/dev/null 2>&1) || fallback 'public checksum verification failed'
done
echo 'hybrid-boot: SD preflight passed (integrity check, not a signature)'
[ "$MODE" != --check ] || exit 0

healthy() {
    bb pidof ak_rtsp_server_sample >/dev/null 2>&1 || return 1
    bb pidof go2rtc >/dev/null 2>&1 || return 1
    bb awk '
        $4 == "0A" {
            if ($2 ~ /:0050$/) web = 1
            if ($2 ~ /:022A$/) rtsp = 1
            if ($2 == "0100007F:216B") sdk = 1
            if ($2 == "0100007F:1F90") httpd = 1
        }
        END { exit !(web && rtsp && sdk && httpd) }
    ' /proc/net/tcp || return 1
    bb awk '$1 == "wlan0" && $2 == "00000000" && $3 != "00000000" { found = 1 }
        END { exit !found }' /proc/net/route || return 1
    bb grep -q 'Video encoder open success' /tmp/sdk-rtsp.log
}

/bin/sh /mnt/stock-probe.sh >/dev/null 2>&1 &
START_PID=$!
elapsed=0
ready=0
while [ "$elapsed" -lt 240 ]; do
    if [ -n "$START_PID" ] && ! stock_bb kill -0 "$START_PID" 2>/dev/null; then
        wait "$START_PID" || fallback 'SD launcher exited unsuccessfully'
        START_PID=
    fi
    if [ -z "$START_PID" ] && healthy; then
        ready=1
        break
    fi
    stock_bb sleep 5
    elapsed=$((elapsed + 5))
done
[ "$ready" = 1 ] || fallback 'SD services not ready within 240 seconds'
echo 'hybrid-boot: SD services ready; monitoring until reboot or interruption'

failures=0
while :; do
    stock_bb sleep 5
    if healthy; then
        failures=0
    else
        failures=$((failures + 1))
        [ "$failures" -lt 3 ] || fallback 'SD services unhealthy on three consecutive checks'
    fi
done