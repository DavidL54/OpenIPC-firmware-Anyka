#!/bin/sh

stock_bb() {
    /bin/busybox "$@"
}

stock_boot() {
    echo 'hybrid-boot: starting internal stock firmware'
    stock_bb umount /media 2>/dev/null || true
    stock_bb umount /mnt 2>/dev/null || true
    exec /bin/busybox sh /etc/init.d/rc.local.stock
}

stock_bb grep -Eq '^[^ ]+ /app squashfs ro([, ]|$)' /proc/mounts ||
    stock_bb mount -t squashfs -o ro /dev/mtdblock4 /app || stock_boot
echo /sbin/mdev > /proc/sys/kernel/hotplug
stock_bb mdev -s || stock_boot
if [ ! -d /sys/module/ak_mci ]; then
    stock_bb insmod /app/ak/modules/ak_mci.ko || stock_boot
fi
attempt=0
while [ ! -b /dev/mmcblk0p1 ] && [ "$attempt" -lt 10 ]; do
    stock_bb sleep 1
    attempt=$((attempt + 1))
done
stock_bb mount -t vfat -o ro /dev/mmcblk0p1 /mnt || stock_boot
[ ! -e /mnt/boot-stock.flag ] || stock_boot
stock_bb mount -t squashfs -o ro /dev/mmcblk0p2 /media || stock_boot
/bin/busybox sh /etc/init.d/hybrid-boot.sh --check || stock_boot
echo 'hybrid-boot: starting validated SD runtime'
/bin/busybox sh /etc/init.d/hybrid-boot.sh --persistent </dev/null >/dev/console 2>&1 &
exit 0