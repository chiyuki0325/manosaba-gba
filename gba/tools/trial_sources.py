"""Incremental original trial voice metadata and managed-text extraction (no audio in ROM)."""
from pathlib import Path
import json
import struct
import UnityPy
from graphics_project import GBA,ROOT,ContentCache,digest,write_changed

BUNDLES=ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64'


def extract():
    cache=ContentCache();voices={};count=0
    for path in sorted(BUNDLES.glob('general-voice-*trial_assets_all.bundle')):
        key=digest(dict(step='trial-voice-length-v1',source=cache.file_key(path)))
        data=cache.get_json(key)
        if data is None:
            print('trial voice durations',path.name,flush=True)
            env=UnityPy.load(str(path));data={}
            for obj in env.objects:
                if obj.type.name=='AudioClip':
                    clip=obj.read();data[clip.m_Name]=round(clip.m_Length*60)
            cache.put_json(key,data)
        voices.update(data);count+=1
    # Persist this small index so the normal compiler never imports UnityPy.
    master_path=BUNDLES/'general-data_assets_all.bundle'
    key=digest(dict(step='trial-master-v1',source=cache.file_key(master_path)))
    master=cache.get_json(key)
    if master is None:
        master={Path(name).stem:ptr.read_typetree() for name,ptr in UnityPy.load(str(master_path)).container.items()}
        cache.put_json(key,master)
    result=dict(voices=voices,bundles=count,master=master,default_hp=10,
                timing=dict(voice_gap_frames=0,fallback_seconds_per_character=.15,fallback_base_seconds=.6),
                provenance=dict(wait='GameAssembly.dll CalculateWaitTime RVA 0x323330: voice length when present; no unconditional extra gap',
                    fallback='CalculateWaitTimeFromMessage RVA 0x3234E0: stripped length * 0.15 + 0.6',
                    hp='resources.assets MasterData last fields: defaultHp=10; retail Damage.Execute RVA 0x325700 is a no-op. Port preserves this no-op; failure is driven by the trial timer.'))
    write_changed(GBA/'assets/source-metadata/trial.json',json.dumps(result,ensure_ascii=False,sort_keys=True)+'\n')
    print('trial voice metadata:',len(voices),'clips,',count,'bundles',flush=True)
    return result


if __name__=='__main__':extract()
