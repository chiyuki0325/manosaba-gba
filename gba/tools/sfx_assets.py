"""Import approved SFX as mono8 samples and short ROM event sequences."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib,json,math,struct,wave
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

GBA=Path(__file__).resolve().parents[1];ROOT=GBA.parent
OUT=GBA/'assets/sfx';AUDIO=GBA/'audio/sfx';FPS=16777216/280896

def frames(seconds):return round(seconds*FPS)
def write(path,data):
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def wav(name,source,loop=False,seconds=None):
    x,rate=sf.read(source,always_2d=True);x=x.mean(axis=1)
    if seconds is not None:x=x[:round(seconds*rate)]
    if rate!=8000:x=resample_poly(x,8000//math.gcd(rate,8000),rate//math.gcd(rate,8000))
    pcm=np.clip(np.rint(x*128+128),0,255).astype('u1').tobytes()
    return pcm_wav(name,pcm,8000,loop)
def pcm_wav(name,pcm,rate,loop=False):
    # smpl is understood by mmutil; looping happens in the existing mixer.
    pcm+=b'\x80'*(len(pcm)&1)
    body=b'fmt '+struct.pack('<IHHIIHH',16,1,1,rate,rate,1,8)
    body+=b'data'+struct.pack('<I',len(pcm))+pcm
    if loop:
        smpl=struct.pack('<9I',0,0,round(1e9/rate),60,0,0,0,1,0)+struct.pack('<6I',0,0,0,len(pcm),0,0)
        body+=b'smpl'+struct.pack('<I',len(smpl))+smpl
    write(AUDIO/(name+'.wav'),b'RIFF'+struct.pack('<I',len(body)+4)+b'WAVE'+body)
    return dict(name=name,frames=frames(len(pcm)/rate),bytes=len(pcm),rate=rate,loop=loop,sha256=hashlib.sha256(pcm).hexdigest())

def cutin_sounds():
    """Cache the original prefab's audio clips and timing alongside its graphics."""
    from graphics_project import ContentCache,digest
    from trial_cutins import BUNDLE,extract,timeline_tracks
    cache=ContentCache();key=digest(dict(step='cutin-audio-v1',source=cache.file_key(BUNDLE)))
    stored=cache.get_json(key)
    if stored:return [(row,cache.path(row['wav'],'.wav')) for row in stored]
    import UnityPy
    data=extract(cache);_,tracks=timeline_tracks(data,data['prefabs']['ObjectionCutIn_Ema'])
    env=UnityPy.load(str(BUNDLE));objects={o.path_id:o for o in env.objects};stored=[]
    for _,track in tracks:
        if not track.get('m_Name','').startswith('Audio Track'):continue
        for clip in track['m_Clips']:
            asset=data['objects'][str(clip['m_Asset']['m_PathID'])]
            audio=objects[asset['m_Clip']['m_PathID']].read()
            raw=next(iter(audio.samples.values()))
            stored.append(dict(name=clip['m_DisplayName'],frame=round(clip['m_Start']*60),
                               duration=clip['m_Duration'],volume=round(asset['m_ClipProperties']['volume']*1024),
                               wav=cache.put_blob(raw,'.wav')))
    cache.put_json(key,stored)
    return [(row,cache.path(row['wav'],'.wav')) for row in stored]

def main():
    OUT.mkdir(parents=True,exist_ok=True);AUDIO.mkdir(parents=True,exist_ok=True)
    inventory=json.loads((ROOT/'sfx/recipes/clips.json').read_text())['clips']
    import sys
    sys.path.insert(0,str(ROOT/'sfx/tools'))
    from trim_sfx_silence import prepare_clips
    trimmed={x['name']:ROOT/x['output'] for x in prepare_clips()}
    samples={};cues={};events=[];tasks=[]
    looped=set()
    for p in (ROOT/'story/zh-Hans').rglob('*.json'):
        for e in json.loads(p.read_text()).get('events',[]):
            if e['op']=='PlaySfxModified' and e['params'].get('Loop'):looped.add(e['params']['SfxPath'])
    def cue(alias,label,sample=None,sequence=None,duration=0,ambient=False,envelope=0):
        cues[alias]=dict(alias=alias,label=label,sample=sample,events=sequence or [],duration=duration,ambient=ambient,envelope=envelope)
    cutin=[]
    for item,path in cutin_sounds():
        key='fx_cutin_'+item['name'].lower();sample=wav(key,path,seconds=item['duration']);samples[key]=sample
        alias='Sfx_CutIn_'+item['name'];cue(alias,item['name']+' / objection cut-in',key,duration=sample['frames'])
        cutin.append(dict(frame=item['frame'],alias=alias,volume=item['volume']))
    for item in inventory:
        alias=item['alias'];name=item['name']
        src=trimmed.get(name) or ROOT/'original_sfx/ambient'/(name+'.wav')
        key='fx_'+alias.lower();loop=alias.startswith('Ambi_') or alias in looped
        tasks.append((key,src,loop,alias,name))
    def convert(t):
        key,src,loop,alias,label=t
        # The credits director uses white noise in 0.4s bursts, not a 10s bed.
        snow=alias=='Ambi_101_1'
        return alias,label,wav(key,src,False if snow else loop,seconds=.4 if snow else None)
    with ThreadPoolExecutor(max_workers=12) as pool:
        for alias,label,sample in pool.map(convert,tasks):
            samples[sample['name']]=sample;cue(alias,label,sample['name'],duration=sample['frames'],ambient=alias.startswith('Ambi_'))
    shared=[('fx_fire','samples/fire.wav',True),('fx_laughter','samples/laughter.wav',True),('fx_train','samples/train.wav',False),('fx_helicopter','samples/helicopter.wav',True)]
    for key,src,loop in shared:
        samples[key]=wav(key,ROOT/'sfx'/src,loop)
    for n in (2,4,6,8):cue(f'Ambi_{n}_1',f'Ambient {n:03d} / shared fire', 'fx_fire',duration=samples['fx_fire']['frames'],ambient=True)
    cue('Ambi_11_1','Laughing girls / 9s loop','fx_laughter',duration=samples['fx_laughter']['frames'],ambient=True)
    for n in (54,105):cue(f'Sfx_{n}',f'Train {n:03d} / shared fade','fx_train',duration=samples['fx_train']['frames'])
    # Both helicopter contexts request a finite one-shot. Keep the original duration,
    # with its authored approach/departure shape applied to the approved 1s loop.
    original=next((ROOT/'original_sfx/sfx').glob('Sfx_Scenario_154 *.wav'));duration=sf.info(original).duration
    cue('Sfx_154','Helicopter / 1s loop + envelope','fx_helicopter',duration=frames(duration),envelope=1)
    cue('Ambi_1_1','Bird ambience / omitted');
    # Atom amplitudes reproduce the approved prototype's RMS reconstruction.
    study=json.loads((ROOT/'sfx/recipes/footsteps.json').read_text())
    for family,plan in study['families'].items():
        atom_rms=[]
        for i in range(1,4):
            path=ROOT/'sfx/samples/footsteps'/f'{family}_atom_{i}.wav'
            x,_=sf.read(path);atom_rms.append(float(np.sqrt(np.mean(x*x))))
            key=f'fx_{family}_{i}';samples[key]=wav(key,path)
        for number,track in plan.items():
            seq=[dict(frame=frames(e['time_seconds']),sample=f'fx_{family}_{e["atom"]}',volume=min(1024,round(1024*e['rms']*track['output_gain']/atom_rms[e['atom']-1])),rate=1024,hold=frames(.3)) for e in track['events']]
            cue('Sfx_'+number,'Footsteps '+number+' / molecules',sequence=seq,duration=frames(track['seconds']))
    # Reuse the approved musical timbres rather than store full bell recordings.
    timbres={'glock':(sf.read(ROOT/'sfx/samples/glockenspiel.wav')[0],629),
             'vibra':(sf.read(ROOT/'sfx/samples/vibraphone.wav')[0],559)}
    for key,(x,base) in timbres.items():
        samples['fx_'+key]=pcm_wav('fx_'+key,np.clip(np.rint(x*128+128),0,255).astype('u1').tobytes(),8363)
    school=[(0,761/2,1),(1,961/2,1),(2,855/2,1),(3,571/2,1),(5,761/2,1),(6,855/2,1),(7,961/2,1),(8,761/2,1)]
    bell=[(0,523,1),(2.95,523,1)]
    crossing=[(.12+.407*i,824,min(1,(i+1)/3,(16-i)/4)) for i in range(16)]
    for n,key,notes,duration in [(33,'glock',bell,6.13),(57,'vibra',school,10.1),(40,'glock',crossing,6.82)]:
        seq=[]
        for i,(start,hz,level) in enumerate(notes):
            hold=duration-start
            if n==40:hold=.4
            if n==57:
                gap=(notes[i+1][0]-start) if i+1<len(notes) else start-notes[i-1][0]
                hold=gap*4/3
                if i+2<len(notes):hold=min(hold,notes[i+2][0]-start)
            seq.append(dict(frame=frames(start),sample='fx_'+key,rate=round(1024*hz/timbres[key][1]),volume=round(level*820),hold=frames(hold)))
        cue(f'Sfx_{n}',f'Bell {n:03d} / '+key,sequence=seq,duration=frames(duration))
    names=sorted(samples);rows=sorted(cues.values(),key=lambda c:c['alias']);lines=['// Generated by tools/sfx_assets.py.','#include "sfx_data.h"','namespace manosaba {','const sfx_note sfx_notes[]={']
    for c in rows:
        c['first']=len(events)
        events+=c['events'];c['count']=len(c['events'])
    for e in events:lines.append('    {'+','.join(str(v) for v in (e['frame'],names.index(e['sample']),e['rate'],e['volume'],e['hold']))+'},')
    lines+=['};','const sfx_cue sfx_cues[]={']
    for c in rows:
        vals=[json.dumps(c['alias']),json.dumps(c['label'],ensure_ascii=False),str(names.index(c['sample']) if c['sample'] else -1),str(c['first']),str(c['count']),str(c['duration']),str(int(c['ambient'])),str(c['envelope']),str(int(samples[c['sample']]['loop']) if c['sample'] else 0)]
        lines.append('    {'+','.join(vals)+'},')
    lines+=['};','const int sfx_cue_count=sizeof(sfx_cues)/sizeof(sfx_cues[0]);','const sfx_cutin_event sfx_cutin_events[]={']
    aliases=[row['alias'] for row in rows]
    for event in cutin:lines.append('    {'+','.join(map(str,(event['frame'],aliases.index(event['alias']),event['volume'])))+'},')
    lines+=['};','const int sfx_cutin_event_count=sizeof(sfx_cutin_events)/sizeof(sfx_cutin_events[0]);','}']
    write(GBA/'src/sfx_data.cpp',('\n'.join(lines)+'\n').encode())
    write(GBA/'src/sfx_samples.inc',(''.join('SFX_SAMPLE('+n+')\n' for n in names)).encode())
    # Delete only our previously generated audio files, not source study assets.
    for p in AUDIO.glob('fx_*.wav'):
        if p.stem not in samples:p.unlink()
    manifest=dict(sample_count=len(names),pcm_bytes=sum(x['bytes'] for x in samples.values()),cue_count=len(rows),sequence_events=len(events),samples=[samples[n] for n in names],cues=rows,cutin_events=cutin,script_alias_overrides={'Sfx_87':'Sfx_12'})
    write(OUT/'manifest.json',(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode())
    print(f'SFX: {len(rows)} aliases, {len(names)} samples, {len(events)} notes, {manifest["pcm_bytes"]:,} PCM bytes')
if __name__=='__main__':main()
