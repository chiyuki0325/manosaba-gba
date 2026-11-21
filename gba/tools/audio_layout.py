#!/usr/bin/env python3
"""Optionally move non-cache Maxmod state out of IWRAM after compiling an object."""
import argparse
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('object')
parser.add_argument('--objcopy', required=True)
parser.add_argument('--readelf', default='readelf')
parser.add_argument('--sequencer-code', action='store_true')
args = parser.parse_args()
sections = re.findall(r'\]\s+(\.bss\.\S+)\s', subprocess.check_output([args.readelf, '-SW', args.object], text=True))
options = []
for section in sections:
    options += ['--rename-section', section + '=.ewram_bss.' + section.removeprefix('.bss.')]
if args.sequencer_code:
    options += ['--rename-section', '.iwram=.ewram']
if options:
    subprocess.run([args.objcopy, *options, args.object], check=True)
