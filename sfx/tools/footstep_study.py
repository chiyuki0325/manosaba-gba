#!/usr/bin/env python3
"""Roughly render footsteps using drum samples already in the XM library."""
from pathlib import Path
import glob
import numpy as np
import soundfile as sf
from scipy.signal import find_peaks
from bell_study import ROOT, RATE, sample_from_xm, render

OUT = ROOT / 'gba' / 'build-generated' / 'sfx' / 'footstep_study'


def onsets(path):
    audio, rate = sf.read(path)
    if audio.ndim == 2:
        audio = audio.mean(axis=1)
    block = rate // 50
    rms = np.sqrt(np.mean(audio[:len(audio)//block*block].reshape(-1, block)**2, axis=1))
    peaks, _ = find_peaks(rms, distance=10, prominence=float(max(rms)) * .12)
    return [(float(i * block / rate), float(rms[i] / max(rms))) for i in peaks], len(audio)/rate


def main():
    OUT.mkdir(exist_ok=True)
    xm = ROOT / 'gba/audio/bgm037_002.xm'
    # XM instrument names, not externally imported drum samples.
    candidates = {'kick': sample_from_xm(xm, 'Bass Drum 8', 0),
                  'tom': sample_from_xm(xm, 'Snare Drum 16', 1)}
    for effect in (51, 71, 73):
        original = next((ROOT/'original_sfx/sfx').glob(f'Sfx_Scenario_{effect:03d} *.wav'))
        strikes, duration = onsets(original)
        for name, sample in candidates.items():
            # An illustrative fixed playback rate, not a tuned matching model.
            notes = [(max(0,t-.03), 1, .65 + .35*strength) for t, strength in strikes]
            audio = render(sample, 1, notes, duration, .18)
            sf.write(OUT/f'sfx_{effect:03d}_{name}.wav', audio, RATE, subtype='PCM_16')
        print(effect, 'detected strikes', len(strikes))


if __name__ == '__main__':
    main()
