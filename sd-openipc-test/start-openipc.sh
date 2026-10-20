#!/bin/sh
set -e

lower_root=${1:-/media}
ram_root=${2:-/tmp/openipc}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

mount -t squashfs -o ro /dev/mmcblk0p2 "$lower_root"
mount -t tmpfs -o mode=0755,size=20M tmpfs /tmp
mkdir -p "$ram_root"
cp -a "$lower_root/." "$ram_root/"

persist_dir="$script_dir/openipc-persist"
if [ -f "$persist_dir/passwd" ] && [ -f "$persist_dir/shadow" ]; then
	cp "$persist_dir/passwd" "$ram_root/etc/passwd"
	cp "$persist_dir/shadow" "$ram_root/etc/shadow"
	chmod 644 "$ram_root/etc/passwd"
	chmod 600 "$ram_root/etc/shadow"
	[ ! -f "$persist_dir/eula-accepted" ] || cp "$persist_dir/eula-accepted" "$ram_root/etc/eula-accepted"
fi

mount -o bind "$ram_root" "$ram_root"

umount /proc
umount /sys
exec "$script_dir/openipc-switch-root" "$ram_root"