#!/tmp/anyka-flash-tools/busybox sh
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib \
 /media/usr/sbin/wpa_supplicant -i wlan0 -D nl80211 \
 -c /tmp/anyka-maintenance/wpa_supplicant.conf \
 >>/tmp/anyka-maintenance/wpa.log 2>&1
