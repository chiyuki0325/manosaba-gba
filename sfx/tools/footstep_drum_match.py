#!/usr/bin/env python3
"""Match drum hits to analysed footstep spectra and render audition files."""
import json
import numpy as np
import soundfile as sf
from bell_study import ROOT, sample_from_xm
from run_footstep_molecules import RATE, WINDOW, load, attacks, window, feature

OUT = ROOT / 'gba/build-generated/sfx/footstep_study/drum_match'


def drum_wave(sample, speed):
    step=8363*speed/RATE
    length=min(WINDOW,int((len(sample)-1)/step))
    wave=np.zeros(WINDOW)
    wave[:length]=np.interp(np.arange(length)*step,np.arange(len(sample)),sample)
    fade=min(160,length)
    wave[length-fade:length]*=np.linspace(1,0,fade)
    rms=np.sqrt(np.mean(wave*wave))
    return wave/max(rms,1e-12)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    xm=ROOT/'gba/audio/bgm037_002.xm'
    drums={
        'kick':sample_from_xm(xm,'Bass Drum 8',0),
        'snare':sample_from_xm(xm,'Snare Drum 16',0),
        'low_tom':sample_from_xm(xm,'Snare Drum 16',1),
        'closed_hihat':sample_from_xm(xm,'Snare Drum 16',2),
        'mid_tom':sample_from_xm(xm,'Snare Drum 16',3),
    }
    candidates=[]
    for name, sample in drums.items():
        for speed in (.6,.8,1.,1.25,1.5,2.):
            wave=drum_wave(sample,speed)
            candidates.append((name,speed,wave,feature(wave)))
    report={}
    for number in (71,173,51,49):
        original,filename=load(number); result=np.zeros(len(original)+WINDOW); events=[]
        for at in attacks(original):
            segment=window(original,at); target=feature(segment)
            name,speed,wave,descriptor=min(candidates,key=lambda c:np.linalg.norm(c[3]-target))
            level=float(np.sqrt(np.mean(segment*segment)))
            result[at:at+WINDOW]+=wave*level
            events.append({'time_seconds':round(at/RATE,3),'drum':name,'speed':speed,'rms':round(level,5),'band_distance':round(float(np.linalg.norm(descriptor-target)),4)})
        result=result[:len(original)]; peak=np.max(np.abs(result)); gain=min(1,.98/peak) if peak else 1
        sf.write(OUT/f'sfx_{number:03d}_drums.wav',result*gain,RATE,subtype='PCM_16')
        report[str(number)]={'source':filename,'gain':round(gain,4),'events':events}
    (OUT/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for n,data in report.items():
        from collections import Counter
        print(n,len(data['events']),dict(Counter(e['drum'] for e in data['events'])))

if __name__=='__main__':main()
