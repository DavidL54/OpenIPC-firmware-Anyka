#!/usr/bin/env python3
"""Build the internal utilities in a temporary path without spaces."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE_SHA = 'b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rescue-tools', action='store_true',
                        help='Static SD/RAM maintenance tool with flashcp; never included in firmware')
    args = parser.parse_args()
    assert hashlib.sha256(args.source.read_bytes()).hexdigest() == SOURCE_SHA
    args.output.mkdir(parents=True, exist_ok=False)
    prefix = os.environ.get('ANYKA_CC_PREFIX', str(HERE.parent.parent /
        'openipc-ak3918-build/output-toolchain-ak3918ev300/host/bin/arm-linux-'))
    with tempfile.TemporaryDirectory(prefix='anyka-busybox-') as temp:
        with tarfile.open(args.source) as source:
            source.extractall(temp, filter='data')
        tree = Path(temp) / 'busybox-1.36.1'
        shutil.copyfile(HERE / 'busybox-internal.config', tree / '.config')
        if args.rescue_tools:
            config = (tree / '.config').read_text()
            for name in ['STATIC', 'FLASHCP']:
                config = config.replace('# CONFIG_' + name + ' is not set', 'CONFIG_' + name + '=y')
                assert 'CONFIG_' + name + '=y' in config
            (tree / '.config').write_text(config)
        with (args.output / 'build.log').open('w') as log:
            for cmd in [['patch', '-p1', '-i', str(HERE / 'busybox-onvif.patch')],
                        ['make', 'CROSS_COMPILE=' + prefix, 'oldconfig'],
                        ['make', '-j4', 'CROSS_COMPILE=' + prefix]]:
                subprocess.run(cmd, cwd=tree, stdin=subprocess.DEVNULL,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        shutil.copyfile(tree / 'busybox', args.output / 'busybox')
        (args.output / 'busybox').chmod(0o755)
        shutil.copyfile(tree / '.config', args.output / 'busybox.config')
    shutil.copyfile(args.source, args.output / 'busybox-1.36.1.tar.bz2')
    report = {'source_url': 'https://busybox.net/downloads/busybox-1.36.1.tar.bz2',
              'source_sha256': SOURCE_SHA, 'version': '1.36.1',
              'busybox_bytes': (args.output / 'busybox').stat().st_size,
              'busybox_sha256': hashlib.sha256((args.output / 'busybox').read_bytes()).hexdigest(),
              'hardware_validated': False}
    report['profile'] = 'static-rescue-with-flashcp' if args.rescue_tools else 'internal-runtime'
    (args.output / 'BUILD-REPORT.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
