#!/tmp/anyka-flash-tools/busybox sh
set -eu
BB=/tmp/anyka-flash-tools/busybox
# The bound inittab is an ordinary tmpfs file. No NOR write occurs here.
"$BB" cat /tmp/anyka-maintenance/running.inittab > /tmp/anyka-maintenance/transition.inittab
exec "$BB" init
