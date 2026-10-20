#!/tmp/anyka-flash-tools/busybox sh
set -eu
BB=/tmp/anyka-flash-tools/busybox
"$BB" sleep 180
[ ! -e /tmp/anyka-maintenance/confirmed ] || exit 0
[ ! -e /tmp/anyka-maintenance/nor-write-started ] || exit 0
actual=$("$BB" sha256sum /dev/mtd6ro | "$BB" awk '{print $1}')
[ "$actual" = f026ef7ecf2318b41804c55dec95a6726962e023a27959f2f7ed8d15ebf6d1ee ] || exit 1
echo 'No SSH confirmation, NOR unchanged: returning to the original boot'
"$BB" sync
"$BB" reboot -f
