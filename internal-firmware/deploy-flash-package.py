#!/usr/bin/env python3
"""Copy the private package to SD, restore read-only, run checks only. No NOR writes."""
import argparse
import hashlib
import io
from pathlib import Path
import re
import subprocess
import tarfile

REMOTE='/mnt/anyka-internal-flash-20261017'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--package',type=Path,required=True)
    ap.add_argument('--camera',default='192.0.2.10')
    ap.add_argument('--log',type=Path,required=True)
    ap.add_argument('--refresh-metadata',action='store_true',help='Update non-binary metadata of an already verified package')
    args=ap.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9.-]+',args.camera)
    package=args.package.resolve()
    for line in (package/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split(maxsplit=1)
        assert hashlib.sha256((package/name).read_bytes()).hexdigest()==digest
    archive=io.BytesIO()
    with tarfile.open(fileobj=archive,mode='w') as tar:
        for p in sorted(package.rglob('*')):
            if args.refresh_metadata and (not p.is_file() or p.relative_to(package).parts[0] in {'tools','install','rollback'}): continue
            tar.add(p,arcname=p.relative_to(package).as_posix(),recursive=False)
    ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10','-o','StrictHostKeyChecking=yes','root@'+args.camera]
    script='''set -eu
LD=/media/lib/ld-musl-arm.so.1
bb() { "$LD" --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
destination=/mnt/anyka-internal-flash-20261017
@DESTINATION_CHECK@
bb awk '$1=="/dev/mmcblk0p1" && $2=="/mnt" && $3=="vfat" && $4 ~ /^ro(,|$)/ {ok=1} END {exit !ok}' /proc/mounts
free_kib=$(bb df -k /mnt | bb awk 'NR==2 {print $4}')
[ "$free_kib" -ge 40000 ]
cleanup() { bb sync; bb mount -o remount,ro /mnt; }
trap cleanup EXIT HUP INT TERM
bb mount -o remount,rw /mnt
@DESTINATION_CREATE@
bb tar -xf - -C "$destination"
bb sync
bb mount -o remount,ro /mnt
(cd "$destination" && bb sha256sum -c SHA256SUMS >/dev/null)
echo PACKAGE_DEPLOYED_SD_READ_ONLY
'''
    if args.refresh_metadata:
        script=script.replace('@DESTINATION_CHECK@','[ -d "$destination" ] && [ ! -L "$destination" ]; (cd "$destination" && bb sha256sum -c SHA256SUMS >/dev/null)').replace('@DESTINATION_CREATE@',':')
    else:
        script=script.replace('@DESTINATION_CHECK@','[ ! -e "$destination" ] && [ ! -L "$destination" ]').replace('@DESTINATION_CREATE@','bb mkdir "$destination"')
    # The shell program is a command argument; stdin contains the archive only.
    import shlex
    result=subprocess.run(ssh+['/bin/sh -c '+shlex.quote(script)],input=archive.getvalue(),
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
    log=result.stdout
    check='''set -eu
LD=/media/lib/ld-musl-arm.so.1
bb() { "$LD" --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"; }
p=/mnt/anyka-internal-flash-20261017
[ ! -e /tmp/anyka-flash-tools ] && [ ! -e /tmp/anyka-program.sh ]
cleanup() { bb rm -f /tmp/anyka-flash-tools/busybox /tmp/anyka-program.sh; bb rmdir /tmp/anyka-flash-tools; }
trap cleanup EXIT HUP INT TERM
bb mkdir -m 700 /tmp/anyka-flash-tools
bb cp "$p/tools/busybox" /tmp/anyka-flash-tools/busybox
bb cp "$p/program-from-ram.sh" /tmp/anyka-program.sh
bb chmod 700 /tmp/anyka-flash-tools/busybox /tmp/anyka-program.sh
/tmp/anyka-flash-tools/busybox sh /tmp/anyka-program.sh "$p" --check
bb awk '$2=="/mnt" {print}' /proc/mounts
bb grep MemAvailable /proc/meminfo
bb pidof go2rtc ak_rtsp_server_sample
'''
    r=subprocess.run(ssh+['/bin/sh'],input=check.encode(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log+=r.stdout
    args.log.write_bytes(log)
    print(log.decode(),end='')
    r.check_returncode()

if __name__=='__main__':main()
