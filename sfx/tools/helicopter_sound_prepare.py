#!/usr/bin/env python3
"""Shorten the helicopter recording and apply an approach/departure envelope."""
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT/'original_sfx/sfx/Sfx_Scenario_154 Helicopter.wav'
OUT = ROOT/'gba/build-generated/sfx/helicopter_study'
RATE = 8000


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    data, sr = sf.read(SOURCE, start=3*48000, frames=int(1.1*48000))
    assert sr == 48000
    if data.ndim == 2:
        data = data.mean(axis=1)
    data = resample_poly(data, RATE, sr)
    cross = RATE // 10
    mix = np.linspace(0, 1, cross, endpoint=False)
    loop = np.concatenate([data[-cross:]*(1-mix) + data[:cross]*mix, data[cross:-cross]])
    path = OUT/'helicopter_154_loop_1s_8k8.wav'
    sf.write(path, loop, RATE, subtype='PCM_U8')
    decoded, _ = sf.read(path)
    # Preview the intended game-controlled envelope; do not bake it into the
    # stored sample, so the player can vary timing and volume at runtime.
    preview = np.tile(decoded, 4)
    attack, release = RATE, int(1.8*RATE)
    preview[:attack] *= np.linspace(0, 1, attack)
    preview[-release:] *= np.linspace(1, 0, release)
    sf.write(OUT/'helicopter_154_loop_4s_preview.wav', preview, RATE, subtype='PCM_16')
    print(f'{len(loop)/RATE:.1f} second loop; {len(loop)} PCM bytes')

if __name__ == '__main__':main()
