#!/usr/bin/env python3
"""Analyse the beginning of Ambient 011 and create a short loop preview."""
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly, stft
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'original_sfx/ambient/Ambient_011_001 Laughing girls.wav'
OUT = ROOT / 'gba/build-generated/sfx/laughter_study'
RATE = 8000


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    data, sr = sf.read(SOURCE, start=0, frames=12*48000)
    assert sr == 48000
    if data.ndim == 2:
        data = data.mean(axis=1)
    audio = resample_poly(data, RATE, sr)
    # First 9.25 s, overlap last 0.25 s with first 0.25 s: 9.0 s total.
    section = audio[:int(9.25*RATE)]
    cross = RATE//4
    f = np.linspace(0, 1, cross, endpoint=False)
    loop = np.concatenate([section[-cross:]*(1-f) + section[:cross]*f, section[cross:-cross]])
    sample_path = OUT/'laughter_011_loop_8k8.wav'
    sf.write(sample_path, loop, RATE, subtype='PCM_U8')
    quantized, _ = sf.read(sample_path)
    sf.write(OUT/'laughter_011_loop_3x_preview.wav', np.tile(quantized,3), RATE, subtype='PCM_16')
    freq,times,z=stft(audio,fs=RATE,nperseg=512,noverlap=448)
    db=20*np.log10(np.maximum(abs(z),1e-6))
    fig,ax=plt.subplots(figsize=(11,4))
    picture=ax.pcolormesh(times,freq,db,shading='auto',cmap='magma',vmin=-85,vmax=-25)
    ax.axvline(9.25,color='cyan',linestyle='--',label='end of source segment')
    ax.set(xlim=(0,12),ylim=(0,4000),xlabel='time (s)',ylabel='frequency (Hz)',title='Ambient 011: first 12 s, mono 8 kHz')
    ax.legend(loc='upper right');fig.colorbar(picture,ax=ax,label='relative dB');fig.tight_layout();fig.savefig(OUT/'first_12s_spectrogram.png',dpi=150);plt.close(fig)
    print(f'{len(loop)/RATE:.2f} s; {len(loop)} PCM bytes')


if __name__=='__main__':main()
