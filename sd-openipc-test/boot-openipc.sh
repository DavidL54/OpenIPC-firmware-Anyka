#!/bin/sh
set -eu

mount -t squashfs -o ro /dev/mmcblk0p2 /media
mount -t tmpfs -o mode=0755,size=3M tmpfs /tmp
mkdir -p /tmp/openipc-etc /tmp/openipc-var-lib
cp -a /media/etc/. /tmp/openipc-etc/
cp -a /media/var/lib/. /tmp/openipc-var-lib/
touch /tmp/openipc-etc/openipc-sdtest
rm -f /tmp/openipc-etc/init.d/S29debugfs

if [ -f /mnt/openipc-persist/wpa_supplicant.conf ]; then
	cp /mnt/openipc-persist/wpa_supplicant.conf /tmp/openipc-etc/wpa_supplicant.conf
	cat > /tmp/openipc-etc/network/interfaces.d/wlan0 <<'EOF'
iface wlan0 inet dhcp
    pre-up wpa_supplicant -B -i wlan0 -D nl80211 -c /etc/wpa_supplicant.conf
    post-down killall -q wpa_supplicant
EOF
	cat > /tmp/openipc-etc/init.d/S41sdwifi <<'EOF'
#!/bin/sh
case "$1" in
	start)
		echo "Starting SD Wi-Fi..."
		(
			ip link set wlan0 up
			wpa_supplicant -B -i wlan0 -D nl80211 -c /etc/wpa_supplicant.conf
			attempt=0
			while [ "$attempt" -lt 20 ]; do
				if wpa_cli -i wlan0 status | grep -q '^wpa_state=COMPLETED$'; then
					udhcpc -i wlan0 -q -n
					exit
				fi
				attempt=$((attempt + 1))
				sleep 1
			done
		) &
		;;
	stop)
		killall -q wpa_supplicant
		killall -q udhcpc
		;;
esac
EOF
	chmod +x /tmp/openipc-etc/init.d/S41sdwifi
fi

cat > /tmp/openipc-etc/init.d/S42httpd <<'EOF'
#!/bin/sh
case "$1" in
	start)
		echo "Starting OpenIPC Web UI..."
		httpd -h /var/www -p 80
		;;
	stop)
		killall -q httpd
		;;
esac
EOF
chmod +x /tmp/openipc-etc/init.d/S42httpd

if [ -f /app/drivers/atbm6132c_wifi_usb.ko ]; then
	insmod /app/ak/modules/usb-common.ko
	insmod /app/ak/modules/usbcore.ko
	insmod /app/ak/modules/ak_hcd.ko
	sleep 1
	insmod /app/drivers/atbm6132c_wifi_usb.ko
fi

mount -o bind /tmp/openipc-etc /media/etc
mount -o bind /tmp/openipc-var-lib /media/var/lib
export OPENIPC_SDTEST=1
umount /proc
umount /sys
exec /mnt/openipc-switch-root /media