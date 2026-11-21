#!/usr/bin/env python3
"""Cut a short seamless loop from Ambient 006 for fire effects."""
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'original_sfx/ambient/Ambient_006_001 Flare up.wav'
OUT = ROOT / 'gba/build-generated/sfx/fire_study'
RATE = 8000


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    data, sample_rate = sf.read(SOURCE, start=10*48000, frames=5*48000)
    assert sample_rate == 48000
    if data.ndim == 2:
        data = np.mean(data, axis=1)
    data = resample_poly(data, RATE, sample_rate)
    cross = RATE // 10
    f = np.linspace(0, 1, cross, endpoint=False)
    loop = np.concatenate([data[-cross:] * (1-f) + data[:cross] * f, data[cross:-cross]])
    sf.write(OUT/'fire_006_loop_8k8.wav', loop, RATE, subtype='PCM_U8')
    quantized, _ = sf.read(OUT/'fire_006_loop_8k8.wav')
    sf.write(OUT/'fire_006_loop_4x_preview.wav', np.tile(quantized, 4), RATE, subtype='PCM_16')
    print('loop seconds', len(loop)/RATE, 'PCM bytes', len(loop))


if __name__ == '__main__':
    main()
