#!/bin/sh
set -eu

device=${1:-/dev/sdb}
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
backup_dir="$repo_dir/sd-backup-before-squashfs-20261005"
package_dir="$repo_dir/sd-openipc-test"
rootfs_image="$package_dir/openipc-sd.sqf"
sdk_rtsp_dir="$package_dir/openipc-persist/sdk-rtsp"
mount_dir=$(mktemp -d)

cleanup() {
	if mountpoint -q "$mount_dir"; then
		umount "$mount_dir"
	fi
	rmdir "$mount_dir"
}
trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
	echo "Lancer ce script avec sudo." >&2
	exit 1
fi

name=${device#/dev/}
case "$name" in
	sd[a-z]) ;;
	*) echo "Peripherique refuse: $device" >&2; exit 1 ;;
esac

[ -b "$device" ] || { echo "$device n'est pas un peripherique bloc." >&2; exit 1; }
[ "$(cat "/sys/block/$name/removable")" = 1 ] || { echo "$device n'est pas amovible." >&2; exit 1; }

size=$(blockdev --getsize64 "$device")
[ "$size" -ge 1800000000 ] && [ "$size" -le 2200000000 ] || {
	echo "Taille inattendue pour $device: $size octets." >&2
	exit 1
}

[ -d "$backup_dir" ] || { echo "Sauvegarde absente: $backup_dir" >&2; exit 1; }
[ -f "$rootfs_image" ] || { echo "Rootfs absent: $rootfs_image" >&2; exit 1; }
[ -f "$package_dir/SHA256SUMS" ] || { echo "Manifeste absent: $package_dir/SHA256SUMS" >&2; exit 1; }
[ -f "$sdk_rtsp_dir/SHA256SUMS" ] || { echo "Runtime RTSP absent: $sdk_rtsp_dir" >&2; exit 1; }

if find "$sdk_rtsp_dir" -type l -print -quit | grep -q .; then
	echo "Le runtime RTSP contient un lien symbolique incompatible avec FAT." >&2
	exit 1
fi

(
	cd "$package_dir"
	sha256sum -c SHA256SUMS
)
(
	cd "$sdk_rtsp_dir"
	sha256sum -c SHA256SUMS
)

echo "Repartitionnement de $device ($size octets)..."
umount "${device}"?* 2>/dev/null || true
wipefs -a "$device"

sfdisk "$device" <<EOF
label: dos
unit: sectors

${device}1 : start=2048, size=524288, type=c, bootable
${device}2 : start=526336, type=83
EOF

partprobe "$device"
udevadm settle
mkfs.vfat -F 32 -n OPENIPC "${device}1"
dd if="$rootfs_image" of="${device}2" bs=4M conv=fsync,notrunc status=progress

mount "${device}1" "$mount_dir"
rsync -rt --modify-window=1 "$backup_dir/" "$mount_dir/"
rsync -rt --modify-window=1 "$package_dir/" "$mount_dir/"
sync

(
	cd "$mount_dir"
	sha256sum -c SHA256SUMS
)
(
	cd "$mount_dir/openipc-persist/sdk-rtsp"
	sha256sum -c SHA256SUMS
)
unsquashfs -s "${device}2"

umount "$mount_dir"
sync
echo "Carte OpenIPC preparee. Suivre COMMANDS-STOCK-SHELL.txt (U-Boot puis shell Linux)."