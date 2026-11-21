#!/usr/bin/env python3
"""Prepare auditionable bird loops at GBA-relevant sample rates."""
from pathlib import Path
from fractions import Fraction
import json
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'original_sfx/ambient/Ambient_001_001 Natural bird.wav'
OUT = ROOT/'gba/build-generated/sfx/bird_study'


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    record={}
    for name,start in [('moderate',10),('dense',25)]:
        source,sr=sf.read(SOURCE,start=start*48000,frames=10*48000)
        assert sr == 48000
        if source.ndim == 2:source=source.mean(axis=1)
        for rate in (8000,15768):
            ratio=Fraction(rate,sr); data=resample_poly(source,ratio.numerator,ratio.denominator)
            n=rate//10; f=np.linspace(0,1,n,endpoint=False)
            loop=np.concatenate([data[-n:]*(1-f)+data[:n]*f,data[n:-n]])
            gain=.8/max(np.max(np.abs(loop)),1e-12); loop*=gain
            out=OUT/f'{name}_{rate}_loop.wav';sf.write(out,loop,rate,subtype='PCM_U8')
            quantized,_=sf.read(out)
            sf.write(OUT/f'{name}_{rate}_3x_preview.wav',np.tile(quantized,3),rate,subtype='PCM_16')
            record[f'{name}_{rate}']={'source_start_seconds':start,'source_end_seconds':start+10,'crossfade_seconds':n/rate,'sample_rate':rate,'frames':len(loop),'duration':len(loop)/rate,'gain_to_0.8_peak':gain,'pcm_bytes':len(loop)}
    (OUT/'analysis.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))

if __name__=='__main__':main()
