#!/usr/bin/env python3
"""Export non-dialogue Unity AudioClips as WAV and inventory other assets."""
from pathlib import Path
import json, re, sys, wave
import UnityPy

root = Path(__file__).resolve().parent.parent
data = root / 'original_game/manosaba_Data'
bundles = data / 'StreamingAssets/aa/StandaloneWindows64'
manifest = []
errors = []
files = sorted(bundles.rglob('*.bundle')) + [data/x for x in ('globalgamemanagers.assets', 'resources.assets', 'sharedassets0.assets', 'level0')]
for i, path in enumerate(files, 1):
    if 'general-voice-' in path.name: # Separate dialogue recordings, not effects or music.
        continue
    try:
        env = UnityPy.load(str(path))
        for obj in env.objects:
            if obj.type.name != 'AudioClip':
                continue
            clip = obj.read()
            name = clip.m_Name
            if path.name.startswith('naninovel-audio_assets_audio-'):
                kind = 'music' if re.search(r'audio-(bgm|song)_', path.name) else 'sfx'
            elif path.name == 'naninovel-spawn_assets_all.bundle' and name in {'Crack_01', 'Crack_02'}:
                kind = 'sfx'
            else:
                kind = 'other'
            if kind == 'other':
                print('OTHER AUDIO', path.relative_to(data), name, flush=True)
                manifest.append({'source':str(path.relative_to(data)), 'name':name, 'kind':kind})
                continue
            samples = clip.samples
            if not samples:
                raise ValueError(f'No decoded samples: {name}')
            for sample_name, sample in samples.items():
                outdir = root / ('original_music' if kind == 'music' else 'original_sfx')
                match = re.search(r'audio-(bgm|song|ambient|sfx)', path.name)
                group = match.group(1) if match else 'sfx'
                outdir = outdir / group
                outdir.mkdir(parents=True, exist_ok=True)
                safe = re.sub(r'[\\/:*?"<>|\x00-\x1f]', '_', Path(sample_name).name)
                out = outdir / safe
                if out.exists():
                    if out.read_bytes() != sample:
                        raise FileExistsError(f'Conflicting output: {out}')
                else:
                    out.write_bytes(sample)
                with wave.open(str(out), 'rb') as wav:
                    frames, rate, channels = wav.getnframes(), wav.getframerate(), wav.getnchannels()
                    if not frames or not rate or not channels: raise ValueError(f'Invalid WAV: {out}')
                manifest.append({'source':str(path.relative_to(data)), 'name':name, 'output':str(out.relative_to(root)), 'frames':frames, 'rate':rate, 'channels':channels})
        if path.name.startswith('naninovel-audio_'):
            print(f'{i}/{len(files)} {path.name}',flush=True)
    except Exception as exc:
        errors.append({'source':str(path.relative_to(data)), 'error':repr(exc)})
        print('ERROR',path,repr(exc),flush=True)
(root/'python_workspace/audio_manifest.json').write_text(json.dumps({'clips':manifest,'errors':errors},ensure_ascii=False,indent=2)+'\n')
print('DONE',len(manifest),'errors',len(errors),flush=True)
sys.exit(bool(errors))
