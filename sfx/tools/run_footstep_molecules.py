#!/usr/bin/env python3
"""Analyse footstep spectra and rebuild footsteps from a few reusable attacks."""
from pathlib import Path
import json
import numpy as np
import soundfile as sf
from scipy.signal import find_peaks, resample_poly
from scipy.cluster.vq import kmeans2
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'gba' / 'build-generated' / 'sfx' / 'footstep_study' / 'molecules'
RATE = 8000
WINDOW = 2400
BANDS = np.array([0, 120, 250, 500, 1000, 2000, 4001])


def load(number):
    path = next((ROOT/'original_sfx/sfx').glob(f'Sfx_Scenario_{number:03d} *.wav'))
    x, sr = sf.read(path)
    if x.ndim == 2:
        x = x.mean(axis=1)
    return resample_poly(x, RATE, sr), path.name


def attacks(x):
    hop = 160
    env = np.sqrt(np.mean(x[:len(x)//hop*hop].reshape(-1,hop)**2, axis=1))
    peaks, _ = find_peaks(env, distance=10, prominence=max(env)*.12)
    return [max(0, int(p*hop - .06*RATE)) for p in peaks]


def window(x, at):
    result = np.zeros(WINDOW)
    n = min(WINDOW, len(x)-at)
    result[:n] = x[at:at+n]
    result[-160:] *= np.linspace(1,0,160)
    return result


def feature(x):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freq = np.fft.rfftfreq(len(x),1/RATE)
    energy = np.array([np.sum(spec[(freq>=lo)&(freq<hi)]) for lo,hi in zip(BANDS[:-1],BANDS[1:])])
    return np.sqrt(energy / (sum(energy)+1e-15))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'sample_rate': RATE, 'clip_seconds': WINDOW/RATE, 'families': {}}
    for name, exemplar, targets in [('outdoor',71,[71,173]), ('underground',51,[51,49])]:
        x, _ = load(exemplar)
        starts = attacks(x)
        clips = [window(x,t) for t in starts]
        features = np.array([feature(c) for c in clips])
        centers, labels = kmeans2(features, 3, minit='++', seed=25, iter=30)
        choices = [int(np.argmin(np.where(labels==i, np.linalg.norm(features-centers[i],axis=1), np.inf))) for i in range(3)]
        templates = [clips[i] for i in choices]
        norms = [np.sqrt(np.mean(a*a)) for a in templates]
        templates = [a/max(n,1e-12) for a,n in zip(templates,norms)]
        for i, wave in enumerate(templates):
            # Normalised templates are stored at fixed peak level for audition.
            sf.write(OUT/f'{name}_atom_{i+1}.wav', .75*wave/max(np.max(np.abs(wave)),1e-12), RATE)
        family = {'template_source':exemplar, 'source_hit_times':[round(starts[i]/RATE,3) for i in choices], 'targets':{}}
        fig, ax = plt.subplots(figsize=(9,3.5))
        for i, idx in enumerate(choices):
            ax.plot(['<120','120-250','250-500','500-1k','1-2k','2-4k'],features[idx]**2,marker='o',label=f'atom {i+1} at {starts[idx]/RATE:.2f}s')
        ax.set(title=f'{name}: energy share in frequency bands',ylabel='energy fraction',xlabel='Hz');ax.legend();fig.tight_layout();fig.savefig(OUT/f'{name}_spectra.png',dpi=150);plt.close(fig)
        for number in targets:
            original, filename = load(number)
            result = np.zeros(len(original)+WINDOW)
            events = []
            for at in attacks(original):
                clip=window(original,at); f=feature(clip)
                i=int(np.argmin(np.linalg.norm(centers-f,axis=1)))
                level=float(np.sqrt(np.mean(clip*clip)))
                result[at:at+WINDOW]+=templates[i]*level
                events.append({'time_seconds':round(at/RATE,3),'atom':i+1,'rms':round(level,5)})
            result=result[:len(original)]
            peak=np.max(np.abs(result)); scale=min(1,.98/peak) if peak else 1
            sf.write(OUT/f'sfx_{number:03d}_reconstructed.wav',result*scale,RATE,subtype='PCM_16')
            family['targets'][str(number)]={'original':filename,'seconds':round(len(original)/RATE,3),'output_gain':round(scale,4),'events':events}
        report['families'][name]=family
    (OUT/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:{'prototype_times':v['source_hit_times'],'event_counts':{n:len(x['events']) for n,x in v['targets'].items()}} for k,v in report['families'].items()},ensure_ascii=False))

if __name__=='__main__': main()
