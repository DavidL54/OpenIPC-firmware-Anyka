#!/bin/sh
# Runs as PID 1's child on the stock rootfs. Nothing here writes NOR.

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs tmpfs /tmp
mount -t jffs2 -o ro /dev/mtdblock5 /conf
mkdir /tmp/conf
cp -a /conf/. /tmp/conf/
umount /conf
mount -t tmpfs -o size=2m tmpfs /conf
cp -a /tmp/conf/. /conf/
mount -t tmpfs tmpfs /var
mkdir -p /var/run/wpa_supplicant /media

/app/ak/load.sh

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/app/bin
export LD_LIBRARY_PATH=/lib:/app/lib
SDK_RTSP=/mnt/openipc-persist/sdk-rtsp
if [ ! -x "$SDK_RTSP/start.sh" ]; then
	if [ -f /mnt/openipc-persist/mdq-tap.so ]; then
		LD_PRELOAD=/mnt/openipc-persist/mdq-tap.so \
			/app/bin/ipc >/tmp/ipc.log 2>&1 &
	else
		/app/bin/ipc >/tmp/ipc.log 2>&1 &
	fi
fi

mount -t squashfs -o ro /dev/mmcblk0p2 /media
MUSL_LD=/media/lib/ld-musl-arm.so.1
MUSL_LIBS=/media/lib:/media/usr/lib
MUSL_BUSYBOX=/media/bin/busybox
insmod /app/ak/modules/usb-common.ko
insmod /app/ak/modules/usbcore.ko
insmod /app/ak/modules/ak_hcd.ko
sleep 1
insmod /app/drivers/atbm6132c_wifi_usb.ko
ifconfig wlan0 up

if [ -f /mnt/openipc-persist/wpa_supplicant.conf ]; then
	WP=/media/usr/sbin/wpa_supplicant
	CF=/mnt/openipc-persist/wpa_supplicant.conf
	$MUSL_LD --library-path $MUSL_LIBS $WP -B -i wlan0 -D nl80211 -c $CF
	udhcpc -i wlan0 -q -n -t 20 -T 3 || echo 'stock-probe: DHCP failed; check Wi-Fi association before retrying'
fi

if [ -x "$SDK_RTSP/start-clock.sh" ]; then
	/bin/sh "$SDK_RTSP/start-clock.sh" || echo 'stock-probe: clock synchronization unavailable'
fi

if [ -x "$SDK_RTSP/start-ssh.sh" ]; then
	/bin/sh "$SDK_RTSP/start-ssh.sh" || echo 'stock-probe: SSH unavailable; inspect /tmp/anyka-ssh.log'
fi

if [ -x "$SDK_RTSP/start.sh" ]; then
	# Two 64 KiB RAM logs; the wrapper retains the encoder startup witness.
	$MUSL_LD --library-path $MUSL_LIBS "$SDK_RTSP/anyka-sdk-log" \
		/tmp/sdk-rtsp.log "$SDK_RTSP/start.sh" &
	sleep 10
	grep -E 'Video encoder open success|RTSP|rtsp' /tmp/sdk-rtsp.log
	"$SDK_RTSP/start-web.sh"
	echo 'stock-probe: RTSP streams: /vs0 and /vs1 on port 554'
	echo 'stock-probe: OpenIPC interface: http://camera-ip/'
else
	sleep 30
	grep -E 'Video encoder open success|Get video stream start' /tmp/ipc.log
	if [ -f /tmp/mdq-tap.log ]; then
		cat /tmp/mdq-tap.log
	fi
	echo 'stock-probe: full log: /tmp/ipc.log'
fi
