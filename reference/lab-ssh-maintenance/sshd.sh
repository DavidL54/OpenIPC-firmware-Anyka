#!/tmp/anyka-flash-tools/busybox sh
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib \
 /media/usr/sbin/dropbear -F -s -j -k -E -m -T 3 -K 60 -I 900 \
 -p 192.0.2.10:22 -P /var/run/anyka-dropbear.pid \
 -r /tmp/anyka-ssh/host_key >>/tmp/anyka-maintenance/sshd.log 2>&1
