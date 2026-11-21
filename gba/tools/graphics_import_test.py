#!/usr/bin/env python3
"""Incremental import behavior, content sharing, source retention and ROM limits."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'python_workspace'))
from graphics_project import ContentCache, check_budget, write_changed
import import_gba_graphics as importer

class GraphicsImportTest(unittest.TestCase):
    def test_warm_plan_and_dialogue_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); source=root/'chapter.json'; cache=ContentCache(root/'cache')
            doc=dict(path='Act01_Chapter01/Act01_Chapter01_Adv01',events=[
                dict(op='ModifyCharacterExtended',line=1,params=dict(IdAndAppearance='Ema.Normal1')),
                dict(op='PrintTextModified',line=2,params=dict(Text='原文<br>换行'))])
            source.write_text(json.dumps(doc)); first=importer.plan(source,cache)
            self.assertEqual(importer.plan(source,cache),first)
            doc['events'][1]['params']['Text']='修改台词'
            source.write_text(json.dumps(doc)); changed=importer.plan(source,ContentCache(root/'cache'))
            self.assertNotEqual(first['source_sha256'],changed['source_sha256'])
            self.assertEqual(first['characters'],changed['characters'])
            self.assertEqual(first['events'][1]['params']['Text'],'原文<br>换行')

    def test_shared_pose_cache_uses_resolved_layers(self):
        rules=dict(default='Root+',compositions={'Normal':'Root+','Same':'Root+'},
                   layers=[dict(name='Root',path=['Root'],tr_id=1,enabled=True)])
        requests=[dict(history=['Normal'],ref=('chapter_a',1)),dict(history=['Same'],ref=('chapter_b',20))]
        with tempfile.TemporaryDirectory() as directory:
            cache=ContentCache(directory)
            with patch.object(importer,'character_rules',return_value=('rules',rules,None)), \
                 patch.object(importer.compose,'load_scene',return_value=object()) as load, \
                 patch.object(importer.compose,'render',return_value=(Image.new('RGBA',(8,8),'white'),None,(0,0,8,8))) as render:
                result=importer.import_layered('Ema',requests,cache,'renderer')
                self.assertEqual(render.call_count,1)
                self.assertEqual(result[('chapter_a',1)]['id'],result[('chapter_b',20)]['id'])
                self.assertEqual(load.call_count,1)
            with patch.object(importer,'character_rules',return_value=('rules',rules,None)), \
                 patch.object(importer.compose,'load_scene',side_effect=AssertionError('warm import opened Unity')), \
                 patch.object(importer.compose,'render',side_effect=AssertionError('warm import rerendered')):
                self.assertEqual(importer.import_layered('Ema',requests,ContentCache(directory),'renderer'),result)

    def test_atomic_unchanged_output_keeps_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'output.json'
            self.assertTrue(write_changed(path,'data')); stamp=path.stat().st_mtime_ns
            self.assertFalse(write_changed(path,'data')); self.assertEqual(path.stat().st_mtime_ns,stamp)

    def test_rom_limits_reserve_audio(self):
        project=dict(budgets=dict(rom_bytes=32<<20,graphics_bytes=16<<20,audio_reserve_bytes=8<<20,other_reserve_bytes=2<<20))
        report=check_budget(16<<20,project,1<<20)
        self.assertEqual(report['projected_total_bytes'],27<<20)
        with self.assertRaises(ValueError):check_budget((16<<20)+1,project)
        with self.assertRaises(ValueError):check_budget(16<<20,project,7<<20)

    def test_comma_only_character_expression(self):
        self.assertEqual(importer.actor_spec('Alisa,ArmR3,ArmL4'),('Alisa','ArmR3,ArmL4'))
        self.assertEqual(importer.actor_spec('Ema.Normal1,ArmR3'),('Ema','Normal1,ArmR3'))

class StageCorpusImportTest(unittest.TestCase):
    def test_reused_scenery_slot_does_not_cover_new_background(self):
        from export_gba_stage import compile_stage,PROPS
        # ADV02: the old Middle slot is freed, then reused by the prison room.
        doc=json.loads((Path(__file__).resolve().parents[2]/'story/zh-Hans/Act01_Chapter01/Act01_Chapter01_Adv02.json').read_text())
        events=[e for e in doc['events'] if e['line']<=75]
        result=compile_stage(events,{}, {('Stills','490_1'):20,('MainBackground','1_1'):21},{},1,2,{})
        images={};orders={};alpha={}
        for value,first,count,kind,target,prop,flags in result['commands']:
            if kind==0:images[target]=value
            elif kind==1 and prop==PROPS.index('opacity'):alpha[target]=result['keys'][first+count-1][0]
            elif kind==3:
                for slot in range(first,first+count):images.pop(slot,None);alpha.pop(slot,None);orders.pop(slot,None)
            elif kind==4:orders[target]=value
        visible=[slot for slot in images if alpha.get(slot,0)>0]
        self.assertEqual(images[max(visible,key=lambda slot:orders[slot])],21)
        self.assertTrue(all(0<=orders[slot]<10 for slot in visible))

    def test_bird_flight_omits_obj_and_preserves_timing(self):
        from graphics_program import presentation_events
        events=[
            dict(op='ModifyCharacterExtended',line=1,params=dict(IdAndAppearance='Warden.1',Rotation=[0,0,-60],Visible=1)),
            dict(op='AnimateActorExtended',line=2,params=dict(ActorIds=['Warden-Front'],ScenePositionY='-100',Duration='1.2')),
            dict(op='Wait',line=3,params=dict(WaitMode='1.2')),
            dict(op='ModifyCharacterExtended',line=4,params=dict(IdAndAppearance='Noah.Normal1',Rotation=[0,0,10],Visible=1)),
            dict(op='ModifyCharacterExtended',line=5,params=dict(IdAndAppearance='Warden.1',Rotation=[0,0,0],Visible=1)),
        ]
        output=presentation_events(events)
        self.assertEqual(output[0]['params']['Visible'],0)
        self.assertEqual(events[0]['params']['Visible'],1)
        self.assertEqual(output[1:],events[1:])

    def test_pose_changes_cut_but_entrances_and_backgrounds_still_animate(self):
        from export_gba_stage import compile_stage, PROPS
        events=[
            dict(op='ModifyCharacterExtended',line=1,params=dict(IdAndAppearance='Ema.Normal1')),
            dict(op='ModifyCharacterExtended',line=2,params=dict(IdAndAppearance='Ema.Smile1',Duration=1,Wait=1)),
            dict(op='HideActors',line=3,params=dict(ActorIds=['Ema'],Duration=1)),
            dict(op='ModifyBackgroundExtended',line=4,params=dict(AppearanceAndTransition='room1')),
            dict(op='ModifyBackgroundExtended',line=5,params=dict(AppearanceAndTransition='room2',Duration=1)),
        ]
        program=compile_stage(events,{10:10,11:11},{('MainBackground','room1'):20,('MainBackground','room2'):21},{},1,2,{0:10,1:11})
        def tracks(line,prop):
            first,count,_=program['events'][program['event_lines'][line]]
            return [key for _,start,size,kind,_,property_,_ in program['commands'][first:first+count]
                    if kind==1 and property_==PROPS.index(prop) for key in program['keys'][start:start+size]]
        self.assertTrue(all(frames==0 and value==256 for value,frames,_,_ in tracks(2,'reveal')))
        self.assertTrue(any(frames>0 and value==256 for value,frames,_,_ in tracks(1,'opacity')))
        self.assertTrue(any(frames>0 and value==0 for value,frames,_,_ in tracks(3,'opacity')))
        self.assertTrue(any(frames>0 and value==256 for value,frames,_,_ in tracks(5,'reveal')))

    def test_trial_picture_program_preserves_picture_lifecycle(self):
        from graphics_program import trial_picture_chapter
        from export_gba_stage import compile_stage,PROPS
        events=[
            dict(op='ModifyCharacterExtended',line=1,params=dict(IdAndAppearance='Ema.Normal1')),
            dict(op='ModifyBackgroundExtended',line=2,params=dict(Id='Stills',AppearanceAndTransition='270_2',Rotation=[0,0,-4],Scale=[1.5],TintColor='#808080')),
            dict(op='PrintDebateText',line=3,params=dict(Text='现场是一幅纯白色的画布')),
            dict(op='ModifyCharacterExtended',line=4,params=dict(IdAndAppearance='Hanna.Normal1',SubId='Flashback')),
            dict(op='ModifyBackgroundExtended',line=5,params=dict(AppearanceAndTransition='Transparent',SubId='Overlay')),
            dict(op='HideActors',line=6,params=dict(ActorIds=['Stills','Hanna-Flashback'],Duration=0.5,Wait=1)),
        ]
        chapter=trial_picture_chapter(dict(events=events,characters=[dict(line=1),dict(line=4)]))
        self.assertEqual([c['line'] for c in chapter['characters']],[4])
        self.assertNotIn(5,[e['line'] for e in chapter['events']])
        result=compile_stage(chapter['events'],{9:9},{('Stills','270_2'):20},{},1,2,{0:9},instant_actor_hide=True)
        first,count,_=result['events'][result['event_lines'][3]]
        self.assertTrue(any(c[3]==8 and c[0]==0 for c in result['commands'][first:first+count]))
        first,count,_=result['events'][result['event_lines'][6]]
        fades=[c for c in result['commands'][first:first+count] if c[3]==1 and c[5]==PROPS.index('opacity')]
        self.assertEqual(len(fades),2)
        self.assertTrue(all(c[6]&128 and result['keys'][c[1]][0]==0 for c in fades))
        self.assertEqual(sorted(result['keys'][c[1]][1] for c in fades),[0,30])
        self.assertTrue(any(c[3]==2 and c[0]==30 for c in result['commands'][first:first+count]))

    def test_solid_foreground_requires_lower_background_variant(self):
        from export_gba_stage import compile_stage
        from graphics_usage import resource_usage
        events=[dict(op='ModifyBackgroundExtended',line=1,params=dict(AppearanceAndTransition='room')),
                dict(op='ModifyBackgroundExtended',line=2,params=dict(AppearanceAndTransition='SolidColor',SubId='Forefront'))]
        p=compile_stage(events,{}, {('MainBackground','room'):0},{},1,2,{})
        p['resources']=['room','white','black']
        _,base=resource_usage([p],dict(room=dict(kind='background'),white=dict(kind='solid'),black=dict(kind='solid')))
        self.assertEqual(base['room'],{256})

    def test_three_standalone_shakes_use_existing_emote(self):
        from export_gba_stage import compile_stage,seconds,CAMERA
        events=[dict(op='ModifyBackgroundExtended',line=1,params=dict(AppearanceAndTransition='room')),
                dict(op='SpawnExtended',line=2,params=dict(Path='ShakeBackground')),
                dict(op='SpawnExtended',line=3,params=dict(Path='ShakeCamera',Params=['','40']))]
        result=compile_stage(events,{}, {('MainBackground','room'):20},{},1,2,{})
        for line,target,duration in ((2,3,seconds(.45)),(3,CAMERA,seconds(6))):
            first,count,_=result['events'][result['event_lines'][line]]
            self.assertIn((2,0,duration,6,target,0,0),result['commands'][first:first+count])

    def test_all_playable_chapters_compile(self):
        from graphics_project import source_paths
        from export_gba_stage import compile_stage
        cache=ContentCache()
        for path in source_paths():
            chapter=importer.plan(path,cache)
            if not chapter['playable']: continue
            mapping={i:i+100 for i in range(len(chapter['characters']))}
            backgrounds={(r['namespace'],r['appearance']):j+10000 for j,r in enumerate(chapter['backgrounds'])}
            rectangles={e['line']:20000+e['line'] for e in chapter['events'] if e['op']=='SpawnExtended' and e.get('params',{}).get('Path')=='MaskingRectangle'}
            with self.subTest(script=chapter['path']):
                result=compile_stage(chapter['events'],{v:v for v in mapping.values()},backgrounds,rectangles,1,2,mapping,script_path=chapter['path'])
                self.assertLess(len(result['events']),65536)
                self.assertLess(len(result['keys']),65536)

if __name__=='__main__':unittest.main()
