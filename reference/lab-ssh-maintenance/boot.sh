#!/tmp/anyka-flash-tools/busybox sh
set -eu
BB=/tmp/anyka-flash-tools/busybox
bb() { "$BB" "$@"; }
exec >>/tmp/anyka-maintenance/boot.log 2>&1
bb ifconfig lo 127.0.0.1 up
bb ifconfig wlan0 192.0.2.10 netmask 255.255.255.0 up
bb route del default 2>/dev/null || :
bb route add default gw 192.0.2.1 wlan0
bb rm -f /var/run/anyka-dropbear.pid /var/run/wpa_supplicant/wlan0
bb date
bb readlink /proc/1/exe
bb touch /tmp/anyka-maintenance/ram-init-ready
# Before any write, an unconfirmed handoff may safely return to the old boot.
"$BB" sh /tmp/anyka-maintenance/handoff-timer.sh </dev/null >>/tmp/anyka-maintenance/boot.log 2>&1 &
echo RAM_NETWORK_CONFIGURED
