#!/usr/bin/env python3
"""Build the bounded native camera API. No network, deployment, or NOR writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
BASE = HERE.parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--static', action='store_true', help='Statically link musl instead of the existing camera loader')
    parser.add_argument('--headless', action='store_true', help='Package without OpenIPC pages; disable UI at startup')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True, mode=0o700)
    if (output / 'anyka-gateway').exists():
        raise SystemExit('Existing executable preserved; choose a new output directory')
    toolchain = Path(os.environ.get('ANYKA_TOOLCHAIN_DIR', str(BASE.parent / 'openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin')))
    strip = os.environ.get('STRIP', str(toolchain / 'arm-linux-strip'))
    readelf = os.environ.get('READELF', str(toolchain / 'arm-linux-readelf'))
    cc = os.environ.get('CC', str(toolchain / 'arm-linux-gcc'))
    vendor = HERE / 'vendor'
    for row in (vendor / 'SHA256SUMS').read_text().splitlines():
        expected, relative = row.split(maxsplit=1)
        assert sha(vendor / relative) == expected, relative
    flags = ['-std=c11', '-D_GNU_SOURCE', '-Os', '-g', '-Wall', '-Wextra', '-Werror',
             '-march=armv5te', '-marm', '-mfloat-abi=soft', '-fno-common', '-fstack-protector-strong',
             '-ffunction-sections', '-fdata-sections', '-D_FORTIFY_SOURCE=2',
             '-DCJSON_NESTING_LIMIT=16', '-DCJSON_CIRCULAR_LIMIT=16',
             '-DMBEDTLS_CONFIG_FILE="crypto-config.h"', '-I'+str(HERE), '-I'+str(vendor / 'mbedtls/include')]
    source = [HERE / (name + '.c') for name in ['main','util','auth','http','audio','webui','preview','mux','ptz']]
    source += [vendor / 'cjson/cJSON.c']
    source += [vendor / 'mbedtls/library' / (name + '.c') for name in ['md5','sha1','base64','platform_util','constant_time']]
    objects = []
    for path in source:
        obj = output / (path.stem + '.o')
        subprocess.run([cc] + flags + ['-c', str(path), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = output / 'anyka-gateway'
    link_flags = ['-static'] if args.static else ['-Wl,-z,relro,-z,now']
    subprocess.run([cc, '-march=armv5te', '-marm'] + objects + link_flags +
                   ['-Wl,--gc-sections', '-Wl,-Map='+str(output/'LINK-MAP.txt'), '-pthread', '-lcrypt', '-lm', '-o', str(binary)], check=True)
    subprocess.run([strip, '--strip-unneeded', str(binary)], check=True)
    details = subprocess.check_output([readelf, '-h', '-l', '-d', '-A', str(binary)], text=True)
    assert 'ARM' in details and 'Tag_CPU_arch: v5TE' in details
    if not args.static:
        assert 'ld-musl-arm.so.1' in details
        assert details.count('(NEEDED)') == 1 and '[libc.so]' in details
    (output/'ELF-REPORT.txt').write_text(details)
    clock = output / 'anyka-clock-compat.so'
    subprocess.run([cc,'-Os','-Wall','-Wextra','-Werror','-march=armv5te','-marm','-mfloat-abi=soft',
                    '-fPIC','-shared','-Wl,-z,relro,-z,now',str(HERE/'clock-compat.c'),'-o',str(clock)],check=True)
    subprocess.run([strip,'--strip-unneeded',str(clock)],check=True)
    details = subprocess.check_output([readelf,'-d','-s',str(clock)],text=True)
    assert details.count('(NEEDED)')==1 and '[libc.so]' in details and 'clock_gettime' in details
    profile = {'profile':'native-headless' if args.headless else 'native-webui', 'implementation':'C', 'gateway_bytes':binary.stat().st_size,
               'gateway_sha256':sha(binary), 'dynamic_libraries':[] if args.static else ['musl libc.so'],
               'ui':not args.headless, 'native_sdk_protocols_preserved':True, 'hardware_validated':False,
               'limits':{'http_clients':6,'preview_clients':1,'preview_frame_bytes':524288,'native_auth_clients':2,'header_bytes':8192,
                         'audio_message_bytes':1280,'sessions':8,'digest_nonces':32},
               'source_sha256':{str(p.relative_to(HERE)):sha(p) for p in source if p.is_relative_to(HERE)},
               'header_sha256':{p.name:sha(p) for p in HERE.glob('*.h')},
               'vendor_manifest_sha256':sha(vendor/'SHA256SUMS'),
               'clock_compat_sha256':sha(clock),'clock_compat_bytes':clock.stat().st_size,
               'clock_compat_source_sha256':sha(HERE/'clock-compat.c'),
               'build_flags':flags}
    (output/'PROFILE.json').write_text(json.dumps(profile,indent=2)+'\n')
    print(json.dumps({'binary':str(binary),'bytes':binary.stat().st_size,'sha256':sha(binary)}))


if __name__ == '__main__':
    main()
