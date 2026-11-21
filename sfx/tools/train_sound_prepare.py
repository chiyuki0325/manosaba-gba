#!/usr/bin/env python3
"""Prepare the shared train sound with its original rise and a fade-out."""
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT/'original_sfx/sfx/Sfx_Scenario_105 Train near.wav'
OUT = ROOT/'gba/build-generated/sfx/train_study'
FADE_SECONDS = 3


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sound, sr = sf.read(SOURCE)
    if sound.ndim == 2:
        sound = sound.mean(axis=1)
    from fractions import Fraction
    for rate, filename, subtype in ((8000, 'train_105_fade_8k8.wav', 'PCM_U8'),
                                    (15768, 'train_105_fade_15768hz_8bit.wav', 'PCM_U8'),
                                    (8000, 'train_105_fade_8k16.wav', 'PCM_16')):
        ratio = Fraction(rate, sr)
        converted = resample_poly(sound, ratio.numerator, ratio.denominator)
        fade = min(len(converted), FADE_SECONDS * rate)
        converted[-fade:] *= np.linspace(1, 0, fade)
        path = OUT/filename
        sf.write(path, converted, rate, subtype=subtype)
        if filename == 'train_105_fade_8k8.wav':
            quantized, _ = sf.read(path)
            nonzero = np.flatnonzero(quantized)
            quantized = quantized[:int(nonzero[-1]) + 1] if len(nonzero) else quantized[:1]
            sf.write(path, quantized, rate, subtype='PCM_U8')
            sf.write(OUT/'train_105_fade_preview.wav', quantized, rate, subtype='PCM_16')
        print(f'{filename}: duration {len(converted)/rate:.3f}s; fade {fade/rate:.1f}s')


if __name__ == '__main__':main()
