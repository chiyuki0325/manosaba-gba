#!/usr/bin/env python3
"""Build the fixed host-side Maxmod converter without changing the toolchain."""
from pathlib import Path
import os
import subprocess
import tarfile

HERE = Path(__file__).resolve().parents[1]
REVISION = '8b0792e45852b58fda4def4d263e246d3a73662d'


def main():
    target = HERE/'build-tools'
    target.mkdir(exist_ok=True)
    archive = target/'mmutil.tar.gz'
    subprocess.run(['curl','--proxy',os.environ.get('MUSIC_PROXY','socks5h://192.168.3.128:2080'),
                    '-fsSL','--retry','2','--max-time','120',
                    'https://codeload.github.com/devkitPro/mmutil/tar.gz/'+REVISION,'-o',str(archive)],check=True)
    with tarfile.open(archive) as tar:
        tar.extractall(target, filter='data')
    source = target/('mmutil-'+REVISION)/'source'
    subprocess.run([os.environ.get('HOST_CC','cc'),'-O2','-Wno-multichar','-DPACKAGE_VERSION="1.9.2"',
                    *map(str,sorted(source.glob('*.c'))),'-lm','-o',str(target/'mmutil')],check=True)
    print('Host mmutil ready:',target/'mmutil')


if __name__ == '__main__':
    main()
