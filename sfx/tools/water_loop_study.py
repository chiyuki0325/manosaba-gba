#!/usr/bin/env python3
"""Cut a shared shower/rain loop from Ambient 002."""
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'original_sfx/ambient/Ambient_002_001 Shower.wav'
OUT = ROOT / 'gba/build-generated/sfx/water_study'
RATE = 8000


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    data, sr = sf.read(SOURCE, start=5*48000, frames=5*48000)
    assert sr == 48000
    if data.ndim == 2:
        data = data.mean(axis=1)
    data = resample_poly(data, RATE, sr)
    cross = RATE // 10
    f = np.linspace(0, 1, cross, endpoint=False)
    loop = np.concatenate([data[-cross:]*(1-f) + data[:cross]*f, data[cross:-cross]])
    path = OUT/'shower_rain_002_loop_8k8.wav'
    sf.write(path, loop, RATE, subtype='PCM_U8')
    quantized, _ = sf.read(path)
    sf.write(OUT/'shower_rain_002_loop_4x_preview.wav', np.tile(quantized,4), RATE, subtype='PCM_16')
    print(f'{len(loop)/RATE:.1f} seconds; {len(loop)} PCM bytes')


if __name__ == '__main__':main()
