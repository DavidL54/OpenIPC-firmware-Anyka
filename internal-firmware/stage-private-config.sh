#!/bin/sh
# Stage secrets on the camera in RAM only. This script never mounts or writes NOR.
# Usage: stage-private-config.sh SD_OPENIPC_PERSIST /tmp/anyka-internal-config-ID SDK-INDEX
set -eu
[ "$#" = 3 ] || { echo 'Expected source, private RAM destination, SDK-INDEX' >&2; exit 2; }
source_dir=$1
destination=$2
index=$3
BB=${BUSYBOX:-/bin/busybox}
bb() { "$BB" "$@"; }
case "$destination" in /tmp/anyka-internal-config-*) ;; *) exit 2 ;; esac
base=${destination#/tmp/}
case "$base" in ''|*[!a-zA-Z0-9_-]*) exit 2 ;; esac
bb awk '$2 == "/tmp" && $3 == "tmpfs" { found=1 } END { exit !found }' /proc/mounts || {
    echo 'Private staging requires /tmp mounted as tmpfs' >&2
    exit 1
}
[ -d "$source_dir" ] && [ ! -L "$source_dir" ]
[ -d "$source_dir/sdk-rtsp" ] && [ ! -L "$source_dir/sdk-rtsp" ]
[ -f "$index" ] && [ ! -L "$index" ]
[ ! -e "$destination" ] && [ ! -L "$destination" ]
umask 077
bb mkdir "$destination" "$destination/openipc-persist"
persist=$destination/openipc-persist
copy_private() {
    [ -f "$1" ] && [ ! -L "$1" ] || { echo 'Private source missing or unsafe' >&2; exit 1; }
    size=$(bb wc -c < "$1")
    [ "$size" -gt 0 ] && [ "$size" -le 16384 ]
    bb cp "$1" "$2"
    bb chmod 600 "$2"
}
copy_private "$source_dir/wpa_supplicant.conf" "$persist/wpa_supplicant.conf"
copy_private "$source_dir/web-access.json" "$persist/web-access.json"
if [ -e "$source_dir/web-ui.conf" ] || [ -L "$source_dir/web-ui.conf" ]; then
    copy_private "$source_dir/web-ui.conf" "$persist/web-ui.conf"
fi
if [ -e "$source_dir/wpa_supplicant.conf.previous" ] || [ -L "$source_dir/wpa_supplicant.conf.previous" ]; then
    copy_private "$source_dir/wpa_supplicant.conf.previous" "$persist/wpa_supplicant.conf.previous"
fi
[ -d "$source_dir/ssh" ] && [ ! -L "$source_dir/ssh" ]
bb mkdir "$persist/ssh"
copy_private "$source_dir/ssh/authorized_keys" "$persist/ssh/authorized_keys"
copy_private "$source_dir/ssh/dropbear_ed25519_host_key" "$persist/ssh/dropbear_ed25519_host_key"
bb mkdir "$persist/sdk-rtsp"
while IFS=' ' read -r kind name; do
    case "$name" in ''|.|..|/*|./*|../*|*/./*|*/../*|*/..|*//*|*[!a-zA-Z0-9_.+/+-]*) exit 1 ;; esac
    case "$kind" in
        D) bb mkdir "$persist/sdk-rtsp/$name" ;;
        F) bb ln -s "/app/runtime/sdk-rtsp/$name" "$persist/sdk-rtsp/$name" ;;
        *) exit 1 ;;
    esac
done < "$index"
# Preserve all supported camera control settings, never a transient privacy flag.
for file in "$source_dir/sdk-rtsp/"*.conf; do
    [ -e "$file" ] || [ -L "$file" ] || continue
    name=${file##*/}
    case "$name" in
        osd-clock.conf|motion-enabled.conf|motion-sensitivity.conf|native-*.conf)
            case "$name" in *[!a-zA-Z0-9_.-]*) exit 1 ;; esac
            [ ! -e "$persist/sdk-rtsp/$name" ] && [ ! -L "$persist/sdk-rtsp/$name" ]
            copy_private "$file" "$persist/sdk-rtsp/$name" ;;
        *) echo 'Unknown camera configuration; review before migration' >&2; exit 1 ;;
    esac
done
bb sha256sum "$index" | bb awk '{ print $1 }' > "$destination/PRIVATE-STAGE-READY"
echo 'Private settings staged in RAM. No NOR write or credential export performed.'
