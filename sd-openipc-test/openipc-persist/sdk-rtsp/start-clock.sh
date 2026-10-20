#!/bin/sh
set -eu
bb() { /media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
if [ -r /var/run/anyka-ntpd.pid ]; then
    read -r pid < /var/run/anyka-ntpd.pid
    if bb kill -0 "$pid" 2>/dev/null; then exit 0; fi
fi
# Client only: ntpd does not listen without -l. Run continuously for drift correction.
bb ntpd -n -p 0.fr.pool.ntp.org -p 1.fr.pool.ntp.org >/dev/null 2>&1 &
echo "$!" > /var/run/anyka-ntpd.pid
