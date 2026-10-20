#!/usr/bin/env python3
"""Assemble a private, gated SD package. No camera access or NOR writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    os.umask(0o077)
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--preparation',type=Path,required=True)
    ap.add_argument('--rescue-tools',type=Path,required=True)
    args=ap.parse_args()
    out=args.preparation.resolve()
    report=json.loads((out/'PREPARATION.json').read_text())
    assert report['rollback']['double_read_identical']
    assert report['private_configuration_image']['all_files_modes_and_links_round_trip']=='passed'
    assert not report['nor_written'] and not report['installable']
    for row in (out/'SHA256SUMS').read_text().splitlines():
        checksum,name=row.split(maxsplit=1)
        assert sha(out/name)==checksum
    tools=out/'tools';tools.mkdir(mode=0o700,exist_ok=True)
    tool_report=json.loads((args.rescue_tools/'BUILD-REPORT.json').read_text())
    assert tool_report['profile']=='static-rescue-with-flashcp'
    assert sha(args.rescue_tools/'busybox')==tool_report['busybox_sha256']
    binary=tools/'busybox'
    shutil.copyfile(args.rescue_tools/'busybox',binary);binary.chmod(0o700)
    details=subprocess.check_output(['readelf','-l','-d',str(binary)],text=True)
    assert 'INTERP' not in details and '(NEEDED)' not in details
    applets=subprocess.check_output(['qemu-arm-static','-cpu','arm926',str(binary),'--list'],text=True).splitlines()
    assert {'flashcp','sh','sha256sum','mount','umount','cp','readlink','sync'}<=set(applets)
    (out/'STATIC-TOOLS-ELF.txt').write_text(details)
    (out/'STATIC-TOOLS-REPORT.json').write_text(json.dumps(tool_report,indent=2)+'\n')
    base=HERE.parent
    candidate=base/report['candidate']
    shutil.copyfile(candidate/'SDK-INDEX',out/'SDK-INDEX')
    entries=json.loads((out/'PRIVATE-TREE-MANIFEST.json').read_text())
    all_files=[];sd_files=[]
    for name,e in entries.items():
        if 'sha256' not in e: continue
        all_files.append(f'{e["sha256"]}  {name}')
        if name.startswith('openipc-persist/'):
            sd_files.append(f'{e["sha256"]}  {name.removeprefix("openipc-persist/")}')
    (out/'CONFIG-FILES-SHA256SUMS').write_text('\n'.join(all_files)+'\n')
    (out/'SD-PRIVATE-SHA256SUMS').write_text('\n'.join(sd_files)+'\n')
    pins={'BUSYBOX_SHA':sha(binary),'SNAPSHOT_SHA':report['rollback']['full_nor_sha256'],
          'ROOT_SHA':sha(out/'install/rootfs-mtd3.bin'),
          'APP_SHA':sha(out/'install/app-mtd4.bin'),
          'CONFIG_SHA':sha(out/'install/config-mtd5.jffs2')}
    for p in report['rollback']['partitions'][:3]:pins[p['partition'].upper()+'_SHA']=p['sha256']
    text=(HERE/'program-from-ram.sh.in').read_text()
    for name,digest in pins.items():text=text.replace('@'+name+'@',digest)
    assert '@' not in text.replace('"$@"','').replace('${3:-}','')
    program=out/'program-from-ram.sh';program.write_text(text);program.chmod(0o700)
    subprocess.run(['qemu-arm-static','-cpu','arm926',str(binary),'sh','-n',str(program)],check=True)
    report['static_ram_tool']={'bytes':binary.stat().st_size,'sha256':sha(binary),
                                'elf':'static ARMv5','arm926_execution':'passed',
                                'flashcp_executed':False}
    report['programming_plan']={'mode':'static RAM init via SSH handoff or UART',
        'default':'checks only','order':['mtd5 configuration and kernel read-only mount check',
        'mtd4 application and readback','mtd3 rootfs and readback'],
        'protected':['mtd0','mtd1','mtd2'], 'automatic_reboot':False,
        'authorization_and_recovery_attestations_present':False}
    (out/'PREPARATION.json').write_text(json.dumps(report,indent=2)+'\n')
    refresh_manifest(out)
    print(json.dumps({'package':str(out),'files':len(list(out.rglob('*'))),
                      'default':'checks only','write_attestations':'absent','nor_written':False},indent=2))

def refresh_manifest(out):
    rows=[f'{sha(p)}  {p.relative_to(out).as_posix()}' for p in sorted(out.rglob('*'))
          if p.is_file() and p.name!='SHA256SUMS']
    (out/'SHA256SUMS').write_text('\n'.join(rows)+'\n')

if __name__=='__main__':main()
