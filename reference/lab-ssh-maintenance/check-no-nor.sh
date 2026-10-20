#!/tmp/anyka-flash-tools/busybox sh
set -eu
BB=/tmp/anyka-flash-tools/busybox
bb() { "$BB" "$@"; }
[ "$(bb readlink /proc/1/exe)" = /tmp/anyka-flash-tools/busybox ]
bb awk '$2=="/etc" && $1=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
bad=0
for file in /proc/[0-9]*/maps; do
    [ -r "$file" ] || continue
    if bb awk '$4 ~ /^1f:0[345]$/ {found=1} END {exit !found}' "$file"; then
        echo "NOR mapping remains: $file" >&2
        bb awk '$4 ~ /^1f:0[345]$/ {print}' "$file" >&2
        bad=1
    fi
done
[ "$bad" = 0 ]
echo RAM_AND_SD_ONLY_USERSPACE
