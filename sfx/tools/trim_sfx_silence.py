#!/usr/bin/env python3
"""Make local one-shot SFX copies with exact digital silence removed."""
from pathlib import Path
import json
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'original_sfx/trimmed_sfx'
MANIFEST = ROOT / 'gba/build-generated/reports/sfx-trim.json'


def prepare_clips():
    OUT.mkdir(parents=True, exist_ok=True)
    entries = []
    clips = json.loads((ROOT/'python_workspace/audio_manifest.json').read_text())['clips']
    for clip in clips:
        if not clip['output'].startswith('original_sfx/sfx/'):
            continue
        src = ROOT/clip['output']
        with wave.open(str(src), 'rb') as wav:
            params = wav.getparams()
            raw = wav.readframes(wav.getnframes())
        samples = np.frombuffer(raw, dtype='<i2').reshape(-1, params.nchannels)
        nonzero = np.flatnonzero(np.any(samples != 0, axis=1))
        end = int(nonzero[-1]) + 1 if len(nonzero) else 1
        removed_tail = len(samples) - end
        target = OUT/src.name
        if removed_tail:
            with wave.open(str(target), 'wb') as wav:
                wav.setparams(params)
                stride = params.nchannels*params.sampwidth
                wav.writeframes(raw[:end*stride])
            output = str(target.relative_to(ROOT))
        else:
            if target.exists():
                target.unlink()
            output = clip['output']
        entries.append({'name':clip['name'], 'source':clip['output'], 'output':output,
                        'rate':params.framerate, 'original_frames':len(samples),
                        'kept_frames':end, 'removed_tail_frames':removed_tail})
    return entries


def main():
    entries=prepare_clips()
    MANIFEST.parent.mkdir(parents=True,exist_ok=True)
    MANIFEST.write_text(json.dumps({'items':entries}, ensure_ascii=False, indent=2)+'\n')
    changed = [x for x in entries if x['removed_tail_frames']]
    tails = sum(x['removed_tail_frames']/x['rate'] for x in changed)
    print(f'{len(entries)} files; {len(changed)} trimmed; {tails:.3f}s trailing exact silence removed')


if __name__ == '__main__':
    main()
