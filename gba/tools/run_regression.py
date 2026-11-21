"""Select a small regression set from changed files; reserve exhaustive patrol for --full."""
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
from contextlib import contextmanager
import argparse,json,os,shutil,subprocess,threading,time
from host_test_cache import CACHE,GBA,build,dependencies,digest,file_hash,locked,sdk_fingerprint

ROOT=GBA.parent
PYTHON=str(ROOT/'python_workspace/.venv/bin/python')
EVIDENCE=('Act01_Chapter02_Trial01','Act02_Chapter03_Trial01','Act02_Chapter03_Trial04')
NATIVE={
 'adv-smoke':('smoke_test',False),'adv-choice':('choice_smoke_test',True),
 'story-retry':('story_retry_game_test',False),
 'actor-panel':('actor_panel_test',False),'overlay-panel':('overlay_panel_test',False),
 'actor-swap':('actor_swap_test',True),'adv-route':('graphics_corruption_test',True),
 'adv-perf':('graphics_perf_test',True),
}
def select(files):
    groups=set();trial_scripts=[]
    for path in files:
        name=Path(path).name
        if name.startswith(('warning_screen','run_warning_screen')):
            groups.add('startup');continue
        if path.startswith('gba/') and (name.startswith('music_room') or name in ('music_collection_test.cpp','run_music_room_test.py','room-titles.tsv')):
            groups.add('music-room');continue
        if name in ('music_player.cpp','music_player.h','music_cues.py','save_system.cpp','save_system.h','save_store.cpp'):
            groups.add('music-room')
        if name in ('gallery.cpp','gallery.h','gallery_game_test.c','run_gallery_test.py','graphics_decode.cpp','graphics_decode.h','graphics_cpp.py','cg_regular_assets.py','cg_regular_policy.py'):
            groups.add('gallery')
        if name in ('stage_player.cpp','stage_player.h','graphics_cpp.py','cg_regular_policy.py','cg_appearance_policy.py','cg_stage_analysis.py','cg_appearance_policy_test.py','cg_appearance_game_test.c','run_cg_appearance_test.py'):
            groups.add('cg-appearance')
        if name in ('graphics_usage.py','encode_gba_graphics.py','image_scene.cpp','stage_player.cpp','adv_instant_test.c','run_adv_instant_test.py'):
            groups.add('adv-instant')
        if path.startswith('gba/') and (name.startswith('sfx_') or name in ('story_sfx.py','run_sfx_test.py') or path.startswith('gba/audio/sfx/')):
            groups.update(('sfx','story-host','trial-workflow'));continue
        if path.startswith('gba/') and (name.startswith(('save_','flash_','rtc_')) or name=='run_save_test.py'):
            groups.update(('save','story-retry'));continue
        if name in ('story_retry_game_test.c','story_retry_size_test.cpp','run_story_retry_test.py'):
            groups.add('story-retry');continue
        if path.startswith('story/zh-Hans/') and '_Trial' in name:
            trial_scripts.append(Path(path).stem);continue
        if name in ('trial_player.cpp','trial_player.h','run_trial_walk.py','trial_walk_test.c','trial_walk_probe_test.c','trial_route_plan.cpp'):
            groups.update(('trial-workflow','trial-evidence','trial-narrator'))
        elif name in ('run_trial_narrator_test.py',):groups.add('trial-narrator')
        elif name in ('host_test_cache.py','run_regression.py','Makefile') and path.startswith('gba/'):
            groups.update(('adv-smoke','trial-workflow'))
        elif name in ('image_scene.cpp','image_scene.h','graphics_decode.cpp','graphics_decode.h','stage_player.cpp','stage_player.h','graphics_cpp.py','actor_cache.py','graphics_program.py'):
            groups.update(('adv-smoke','adv-choice','actor-panel','overlay-panel','actor-swap','adv-route','graphics-budget'))
        elif name=='graphics_banks_test.c':groups.add('adv-entry-sample')
        elif name=='graphics_budget_scan.py' or name=='graphics_budget_scan.cpp':groups.add('graphics-budget')
        elif name in ('story_vm.cpp','story_vm.h','story_format.h','story.py','trial_story.py','story_layout.py','story_scene.cpp','story_scene.h') or path.startswith('story/zh-Hans/'):
            groups.update(('story-host','adv-smoke','adv-choice','story-retry','trial-workflow','trial-evidence'))
        elif name.startswith(('witch_book','book_')) and path.startswith('gba/src/'):
            groups.update(('witch-book','trial-evidence'))
        elif path.startswith('gba/') and (name.startswith(('music_','music.')) or path.startswith('gba/audio/') or name=='Butano.mk'):
            groups.update(('music','trial-workflow'))
        elif path.startswith('gba/src/'):
            groups.update(('adv-smoke','trial-workflow'))
    if trial_scripts:groups.add('trial-changed')
    return groups,sorted(set(trial_scripts))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--jobs',type=int,default=min(12,os.cpu_count() or 1))
    parser.add_argument('--changed',action='append',help='override git diff selection; repeat for several paths')
    parser.add_argument('--base',default='HEAD',help='compare the working tree against this revision')
    parser.add_argument('--full',action='store_true',help='all trial entry/continuous routes, all ADV entrances and performance')
    parser.add_argument('--fresh',action='store_true',help='execute tests even if identical inputs passed')
    parser.add_argument('--list',action='store_true',help='print selection without building or emulating')
    args=parser.parse_args();started=time.monotonic()
    if args.jobs<1:parser.error('jobs must be positive')
    if args.changed is None:
        changed=subprocess.check_output(['git','diff','--name-only',args.base],cwd=ROOT,text=True).splitlines()
        changed+=subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines()
    else:changed=args.changed
    groups,trial_scripts=select(changed)
    if args.full:
        groups.update((*NATIVE,'adv-entries','story-host','trial-workflow','trial-full','trial-narrator','graphics-budget','graphics-footprint','save','sfx','gallery','cg-appearance','adv-instant','music-room','startup'))
        groups.discard('adv-entry-sample');groups.discard('trial-evidence');groups.discard('trial-changed')
    print('Selected: '+(', '.join(sorted(groups)) or 'none (no affected runtime tests)'),flush=True)
    if args.list:return
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if not groups:
        (out/'report.json').write_text(json.dumps(dict(status='no_runtime_changes',tests=0,changed=changed),indent=2)+'\n');return
    source=args.rom.resolve();rom_hash=file_hash(source);elf_hash=file_hash(source.with_suffix('.elf'))
    frozen=CACHE/'roms'/(rom_hash+'-'+elf_hash[:12]);frozen.mkdir(parents=True,exist_ok=True)
    rom=frozen/'runtime.gba'
    with locked(frozen/'copy.lock'):
        if not rom.exists():shutil.copy2(source,rom);shutil.copy2(source.with_suffix('.elf'),rom.with_suffix('.elf'))
    symbols=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda label:next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+label))
    profile=address('_ZN8manosaba20graphics_performanceE');banks=address('_ZN8manosaba14graphics_banksE')
    workers=min(args.jobs,6,len(groups))
    condition=threading.Condition();available=args.jobs
    def weight(group):
        if group=='trial-full':return max(1,args.jobs*2//3)
        if group=='save':return min(2,args.jobs)
        if group=='adv-entries':return max(1,args.jobs-max(1,args.jobs*2//3))
        if group in ('trial-evidence','adv-entry-sample'):return min(3,args.jobs)
        if group=='trial-changed':return min(3,args.jobs,len(trial_scripts))
        return 1
    @contextmanager
    def slots(count):
        nonlocal available
        with condition:
            condition.wait_for(lambda:available>=count);available-=count
        try:yield
        finally:
            with condition:available+=count;condition.notify_all()

    def inputs(group):
        sources=[Path(__file__),GBA/'tools/host_test_cache.py']
        if group in NATIVE:sources.append(GBA/f'tools/{NATIVE[group][0]}.c')
        elif group.startswith('adv-entr'):sources.append(GBA/'tools/graphics_banks_test.c')
        else:
            scripts={
             'trial-workflow':['run_trial_game_test.py','trial_game_test.c'],
             'trial-evidence':['run_trial_walk.py','trial_walk_test.c','trial_walk_probe_test.c','trial_route_plan.cpp'],
             'trial-changed':['run_trial_walk.py','trial_walk_test.c','trial_walk_probe_test.c','trial_route_plan.cpp'],
             'trial-full':['run_trial_walk.py','trial_walk_test.c','trial_walk_probe_test.c','trial_route_plan.cpp'],
             'trial-narrator':['run_trial_narrator_test.py','run_trial_walk.py','trial_walk_test.c','trial_route_plan.cpp'],
             'graphics-budget':['graphics_budget_scan.py','graphics_budget_scan.cpp','obj_silhouettes.py'],
             'graphics-footprint':['graphics_budget_scan.py','graphics_budget_scan.cpp','obj_silhouettes.py'],
             'story-host':['story_reference.py','story.py','trial_story.py','story_layout.py','story_music.py','witch_book_catalog.py','story_test.cpp'],
             'witch-book':['run_witch_book_test.py','witch_book_game_test.c'],
             'music':['run_hiro_game_test.py','hiro_game_test.c'],
             'startup':['run_warning_screen_test.py','warning_screen_game_test.c'],
             'music-room':['run_music_room_test.py','music_room_game_test.c','music_collection_test.cpp','music_player_test.cpp','save_store_test.cpp'],
             'sfx':['run_sfx_test.py','sfx_game_test.c','sfx_player_test.cpp','sfx_assets.py','story_sfx.py'],
             'save':['run_save_test.py','save_store_test.cpp','save_snapshot_test.cpp','save_game_test.c'],
             'gallery':['run_gallery_test.py','gallery_game_test.c'],
             'cg-appearance':['run_cg_appearance_test.py','cg_appearance_game_test.c','cg_appearance_policy_test.py','cg_appearance_policy.py','cg_stage_analysis.py'],
             'adv-instant':['run_adv_instant_test.py','adv_instant_test.c'],
            }[group]
            sources.extend(GBA/'tools'/p for p in scripts)
        if group in ('story-host','trial-evidence','trial-changed','trial-full','trial-narrator'):
            sources.extend((GBA/'src/story_vm.cpp',GBA/'src/lzss.cpp',GBA/'src/story_data.cpp'))
        deps=dependencies(sources)
        assets={
            'trial-workflow':['assets/trial/camera/Court/expected.bin','assets/trial/camera.json','assets/trial/reduced/podium-forbidden.bin'],
            'trial-narrator':['src/font_data.c'],
            'startup':['tools/warning_screen_mockup.py','tools/font.py','assets/fusion-pixel-12px-monospaced-zh_hans.bdf','assets/unifont-17.0.03.hex.gz'],
            'music':['src/story_data.cpp','assets/music/catalog.json'],
            'sfx':['assets/sfx/manifest.json','src/sfx_player.cpp','src/sfx_data.cpp','src/sfx_backend.cpp'],
            'story-host':['assets/text-layout.json'],
            'graphics-budget':['assets/graphics-cache/linked/graphics.json','assets/graphics-cache/linked/graphics.bin','assets/graphics/catalog.json','src/stage_player.cpp','src/stage_player.h'],
            'graphics-footprint':['assets/graphics-cache/linked/graphics.json','assets/graphics-cache/linked/graphics.bin','assets/graphics/catalog.json','src/stage_player.cpp','src/stage_player.h'],
        }.get(group,())
        deps.update({str(GBA/p):file_hash(GBA/p) for p in assets})
        if group=='story-host':deps.update({str(p):file_hash(p) for p in sorted((ROOT/'story/zh-Hans').glob('*/*.json'))})
        return deps

    def execute(group,directory):
        log=[];inner=weight(group)
        def command(argv):
            run=subprocess.run(list(map(str,argv)),capture_output=True,text=True,cwd=ROOT,timeout=900)
            log.append(run.stdout+run.stderr)
            if run.returncode:raise subprocess.CalledProcessError(run.returncode,argv,run.stdout,run.stderr)
        env=dict(os.environ,MANOSABA_ROM_SHA256=rom_hash)
        if group in NATIVE:
            name,metric=NATIVE[group];executable=build(name,[GBA/f'tools/{name}.c'])
            extra=[profile] if metric else []
            if group=='actor-swap':extra.append(banks)
            run=subprocess.run([str(executable),str(rom),str(directory),*extra],env=env,capture_output=True,text=True,timeout=900)
            log.append(run.stdout+run.stderr)
            if run.returncode:raise subprocess.CalledProcessError(run.returncode,group,run.stdout,run.stderr)
        elif group.startswith('adv-entr'):
            executable=build('graphics_banks_test',[GBA/'tools/graphics_banks_test.c'])
            ranges=[(i,min(230,i+20)) for i in range(0,230,20)] if group=='adv-entries' else [(i,i+1) for i in (0,3,34,120,229)]
            def shard(pair):
                first,end=pair;target=directory/str(first);target.mkdir(exist_ok=True)
                run=subprocess.run([str(executable),str(rom),str(target),profile,str(first),str(end)],capture_output=True,text=True,timeout=900)
                (target/'run.log').write_text(run.stdout+run.stderr)
                return run.returncode
            with ThreadPoolExecutor(max_workers=inner) as pool:codes=list(pool.map(shard,ranges))
            if any(codes):raise RuntimeError('an ADV entrance shard failed')
        elif group in ('trial-evidence','trial-changed','trial-full'):
            matches=EVIDENCE if group=='trial-evidence' else trial_scripts if group=='trial-changed' else ()
            argv=[PYTHON,GBA/'tools/run_trial_walk.py',rom,directory,'--scope','both' if group=='trial-full' else 'segment','--jobs',str(inner)]
            for match in matches:argv+=['--match',match]
            if args.fresh:argv+=['--fresh']
            command(argv)
        elif group=='trial-narrator':
            plan=directory/'plan'
            command([PYTHON,GBA/'tools/run_trial_walk.py',rom,plan,'--scope','segment','--match','Act02_Chapter01_Trial06','--plan-only'])
            command([PYTHON,GBA/'tools/run_trial_narrator_test.py',rom,plan/'fixtures.json',directory])
        elif group in ('trial-workflow','witch-book','music','music-room','startup','save','sfx','gallery','cg-appearance','adv-instant'):
            script={'startup':'run_warning_screen_test.py','trial-workflow':'run_trial_game_test.py','witch-book':'run_witch_book_test.py','music':'run_hiro_game_test.py','music-room':'run_music_room_test.py','save':'run_save_test.py','sfx':'run_sfx_test.py','gallery':'run_gallery_test.py','cg-appearance':'run_cg_appearance_test.py','adv-instant':'run_adv_instant_test.py'}[group]
            argv=[PYTHON,GBA/'tools'/script,rom,directory]
            if group=='trial-workflow' and not args.full:argv+=['--quick']
            command(argv)
        elif group.startswith('graphics-'):
            argv=[PYTHON,GBA/'tools/graphics_budget_scan.py','--variant=scale-snap115','--output='+str(directory)]
            if group=='graphics-footprint':argv+=['--footprint']
            command(argv)
        elif group=='story-host':
            trace=directory/'traces.txt';command([PYTHON,GBA/'tools/story_reference.py',trace])
            executable=build('story_test',[GBA/'tools/story_test.cpp',GBA/'src/story_vm.cpp',GBA/'src/lzss.cpp',GBA/'src/story_data.cpp'],compiler='g++',
                flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src')),libraries=())
            command([executable,trace])
        (directory/'run.log').write_text(''.join(log))

    def run(group):
        key=digest(dict(group=group,rom=rom_hash,elf=elf_hash,inputs=inputs(group),sdk=sdk_fingerprint(),quick=group=='trial-workflow' and not args.full,trial_scripts=trial_scripts if group=='trial-changed' else []))
        directory=CACHE/'regression'/key;directory.mkdir(parents=True,exist_ok=True)
        with locked(directory/'run.lock'):
            saved=directory/'passed.json'
            if not args.fresh and saved.exists():return dict(json.loads(saved.read_text()),cached=True,seconds=0)
            start=time.monotonic();passed=True
            try:
                with slots(weight(group)):execute(group,directory)
            except (subprocess.CalledProcessError,subprocess.TimeoutExpired,RuntimeError) as error:
                passed=False;(directory/'failure.log').write_text(str(error)+'\n'+str(getattr(error,'stdout',''))+'\n'+str(getattr(error,'stderr','')))
            result=dict(name=group,passed=passed,cached=False,seconds=round(time.monotonic()-start,3),directory=str(directory),input_signature=key)
            if passed:saved.write_text(json.dumps(result,indent=2)+'\n')
            return result
    results=[]
    order=sorted(groups,key=lambda g:({'trial-full':100,'adv-entries':90,'adv-perf':80}.get(g,1)),reverse=True)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for future in as_completed([pool.submit(run,g) for g in order]):
            result=future.result();results.append(result)
            print(f'{result["name"]}: {"PASS" if result["passed"] else "FAIL"}{" (cached)" if result["cached"] else ""}, {result["seconds"]}s',flush=True)
    report=dict(rom_sha256=rom_hash,changed=changed,full=args.full,tests=len(results),passed=sum(r['passed'] for r in results),
        reused=sum(r['cached'] for r in results),wall_seconds=round(time.monotonic()-started,3),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(f'{report["passed"]}/{report["tests"]} passed, {report["reused"]} reused; {report["wall_seconds"]}s',flush=True)
    if report['passed']!=report['tests']:raise SystemExit(1)

if __name__=='__main__':main()
