#!/bin/sh
# Prepare RAM handoff over SSH. This script never writes NOR.
set -eu
OLD_LD=/media/lib/ld-musl-arm.so.1
oldbb() { "$OLD_LD" --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
BB=/tmp/anyka-flash-tools/busybox
pkg=/mnt/anyka-internal-flash-20261017
[ ! -e /tmp/anyka-flash-tools ]
[ -d /tmp/anyka-maintenance ] && [ ! -L /tmp/anyka-maintenance ]
[ "$(oldbb readlink /proc/1/exe)" = /bin/busybox ]
[ "$(oldbb tr '\000' ' ' < /proc/386/cmdline)" = '/bin/busybox sh /etc/init.d/hybrid-boot.sh --persistent ' ]
[ -s /tmp/anyka-ssh/host_key ] && [ -s /tmp/anyka-ssh/root/.ssh/authorized_keys ]
(cd "$pkg" && oldbb sha256sum -c SHA256SUMS >/dev/null)
(cd /mnt/openipc-persist && oldbb sha256sum -c "$pkg/SD-PRIVATE-SHA256SUMS" >/dev/null)
oldbb mkdir -m 700 /tmp/anyka-flash-tools
oldbb cp "$pkg/tools/busybox" "$BB"
oldbb chmod 700 "$BB"
bb() { "$BB" "$@"; }
[ "$(bb sha256sum "$BB" | bb awk '{print $1}')" = 21c6a3bd314f62d0aee09f57774639149fbe44d44dbca231de34f547b4ee044e ]
for s in /tmp/anyka-maintenance/*.sh; do "$BB" sh -n "$s"; done
umask 077
bb cp /mnt/openipc-persist/wpa_supplicant.conf /tmp/anyka-maintenance/wpa_supplicant.conf
bb cp /etc/inittab /tmp/anyka-maintenance/original.inittab
# Keep the full configuration directory in RAM, including passwd and inittab.
bb mkdir -m 700 /tmp/anyka-maintenance/etc
bb cp -a /etc/. /tmp/anyka-maintenance/etc/
bb mount --bind /tmp/anyka-maintenance/etc /etc
bb mount --bind "$BB" /bin/busybox
bb mount --bind /tmp/anyka-maintenance/transition.inittab /etc/inittab
bb kill -STOP 386
# Close the encoder normally before init's global process termination.
for process in go2rtc ak_rtsp_server_sample; do
    for p in $(bb pidof "$process" || :); do bb kill -TERM "$p"; done
done
bb sleep 5
! bb pidof go2rtc ak_rtsp_server_sample >/dev/null
bb kill -HUP 1
n=0
while [ ! -e /tmp/anyka-maintenance/reload-ack ] && [ "$n" -lt 10 ]; do bb sleep 1; n=$((n+1)); done
[ -e /tmp/anyka-maintenance/reload-ack ]
# SSH will close during this operation. The new RAM init restarts Wi-Fi/SSH.
bb setsid "$BB" sh -c '"$0" sleep 2; "$0" kill -QUIT 1' "$BB" </dev/null >/tmp/anyka-maintenance/takeover.log 2>&1 &
echo RAM_HANDOFF_SCHEDULED
