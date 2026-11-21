#!/usr/bin/env python3
"""Fetch the pinned upstream engine; toolchains are installed separately."""
from pathlib import Path
import subprocess

VERSION = '21.10.0'
COMMIT = '7443b6c389c086f6cf3222674366917e8e8fc457'
DEST = Path(__file__).resolve().parents[1] / 'vendor/butano'


def main():
    if not DEST.exists():
        subprocess.run(['git', 'clone', '--depth', '1', '--branch', VERSION,
                        'https://github.com/GValiente/butano.git', str(DEST)], check=True)
    actual = subprocess.check_output(['git', '-C', str(DEST), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != COMMIT:
        raise SystemExit(f'Expected Butano {COMMIT}, found {actual} in {DEST}')
    print(f'Butano {VERSION}: {actual}')


if __name__ == '__main__':
    main()
