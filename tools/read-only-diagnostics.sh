#!/bin/sh
# Run manually on an accessible camera. No write, restart or deployment.
# Review output for local deployment identity before sharing it publicly.
set -eu
uname -a
cat /proc/cpuinfo
cat /proc/meminfo
cat /proc/mtd
cat /proc/mounts
if [ -f /tmp/anyka-osd-clock.json ]; then
    head -c 4096 /tmp/anyka-osd-clock.json
    echo
fi
# Never print password hashes, Wi-Fi configuration, credentials or keys.
