#!/bin/sh
# Key-only SSH on the SD runtime. Host keys and authorization stay private.
set -eu
bb() {
    /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
}
PRIVATE=/mnt/openipc-persist/ssh
RUNTIME=/tmp/anyka-ssh
[ -s "$PRIVATE/authorized_keys" ] && [ ! -L "$PRIVATE/authorized_keys" ] || {
    echo 'SSH disabled: private authorized_keys missing or unsafe' >&2
    exit 1
}
[ -s "$PRIVATE/dropbear_ed25519_host_key" ] && [ ! -L "$PRIVATE/dropbear_ed25519_host_key" ] || {
    echo 'SSH disabled: private host key missing or unsafe' >&2
    exit 1
}
if [ -s /var/run/anyka-dropbear.pid ] &&
    bb kill -0 "$(bb cat /var/run/anyka-dropbear.pid)" 2>/dev/null; then
    exit 0
fi
umask 077
bb mkdir -p "$RUNTIME/root/.ssh"
bb chmod 700 "$RUNTIME" "$RUNTIME/root" "$RUNTIME/root/.ssh"
bb cp "$PRIVATE/authorized_keys" "$RUNTIME/root/.ssh/authorized_keys"
bb cp "$PRIVATE/dropbear_ed25519_host_key" "$RUNTIME/host_key"
bb chmod 600 "$RUNTIME/root/.ssh/authorized_keys" "$RUNTIME/host_key"
# The stock root account lives at /. Its RAM home provides safe key permissions
# without writing the read-only NOR rootfs or changing its password.
if ! bb grep -q ' /etc/passwd ' /proc/mounts; then
    bb awk -F: 'BEGIN { OFS=":" } $1 == "root" { $6="/tmp/anyka-ssh/root" } { print }' \
        /etc/passwd > "$RUNTIME/passwd"
    bb chmod 644 "$RUNTIME/passwd"
    bb mount --bind "$RUNTIME/passwd" /etc/passwd
fi
bb grep -q '^root:[^:]*:0:0:[^:]*:/tmp/anyka-ssh/root:/bin/sh$' /etc/passwd || {
    echo 'SSH disabled: unexpected root account layout' >&2
    exit 1
}
# ifconfig is used rather than the stock bootloader or persistent environment.
IP=$(bb ifconfig wlan0 | bb awk '/inet addr:/ {
    sub(/.*inet addr:/, ""); sub(/ .*/, ""); print; exit
}')
[ -n "$IP" ] || { echo 'SSH disabled: wlan0 has no IPv4 address' >&2; exit 1; }
export ANYKA_SSH_IP="$IP"
bb cat > "$RUNTIME/server.sh" <<'EOF'
exec /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib \
    /media/usr/sbin/dropbear -F -s -j -k -E -m -T 3 -K 60 -I 900 \
    -p "$ANYKA_SSH_IP:22" -P /var/run/anyka-dropbear.pid -r /tmp/anyka-ssh/host_key
EOF
/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib \
    /mnt/openipc-persist/sdk-rtsp/anyka-sdk-log /tmp/anyka-ssh.log "$RUNTIME/server.sh" &
bb sleep 1
[ -s /var/run/anyka-dropbear.pid ] && \
    bb kill -0 "$(bb cat /var/run/anyka-dropbear.pid)"
