"""Generate verified trial routes, then patrol a frozen ROM with real mGBA input.

Independent native processes run in parallel; neither assets nor ROM are rebuilt.
The output includes fixtures, per-route events, native states and a failure list.
"""
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
import argparse,hashlib,html,json,os,re,shutil,subprocess,time
from host_test_cache import CACHE,build,digest,file_hash,locked,planned_routes

GBA=Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def summarize_issues(results,out):
    """Coalesce a block seen both at direct entry and on a continuous route."""
    grouped={}
    for result in results:
        records=[]
        if result['status']!='passed':records.append(dict(kind=result['status'],script=result.get('script'),pc=result.get('pc'),state=result.get('state'),checkpoint=result.get('checkpoint')))
        events=out/result.get('directory','')/'events.jsonl'
        if events.exists():
            for row in events.read_text().splitlines():
                event=json.loads(row)
                if event.get('missing_pixels',0):records.append(dict(event,kind='missing_pixels'))
        for record in records:
            key=(record['kind'],record.get('script'),record.get('pc'),record.get('state'))
            if record.get('script') is None:key+=(result['path'],)
            issue=grouped.setdefault(key,dict(record,occurrences=[]))
            issue['occurrences'].append(dict(path=result['path'],scope=result.get('scope'),directory=result.get('directory')))
    return list(grouped.values())

def main():
    started=time.monotonic()
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--jobs',type=int,default=min(12,os.cpu_count() or 1))
    p.add_argument('--match',action='append',default=[],help='substring of canonical trial path; repeat to select several')
    p.add_argument('--scope',choices=('segment','chapter','both'),default='chapter',help='continuous chapters by default; both adds independent entry coverage')
    p.add_argument('--plan-only',action='store_true',help='write fixtures without booting the emulator')
    p.add_argument('--fresh',action='store_true',help='execute even when identical inputs already passed')
    p.add_argument('--special',action='store_true',help='capture trial maps and pictures')
    p.add_argument('--special-restores',action='store_true',help='also exercise overlays and Flash restore during maps/pictures')
    p.add_argument('--limit',type=int,help='run only the first N selected routes')
    p.add_argument('--max-frames',type=int,default=200000)
    p.add_argument('--timeout',type=int,default=900,help='host seconds per independent route')
    args=p.parse_args()
    if args.jobs<1 or (args.limit is not None and args.limit<1):p.error('jobs and limit must be positive')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=args.rom.resolve();elf=source.with_suffix('.elf')
    rom=out/'patrol.gba';frozen_elf=rom.with_suffix('.elf')
    if source!=rom:shutil.copy2(source,rom);shutil.copy2(elf,frozen_elf)
    rom_hash=sha(rom)
    symbols=subprocess.check_output([os.environ.get('ARM_NM','/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'),'-S',str(frozen_elf)],text=True)
    def symbol(name,expected_size):
        row=next(line.split() for line in symbols.splitlines() if line.endswith(' '+name))
        if int(row[1],16)!=expected_size:
            raise SystemExit(f'{name} layout differs: rebuild this ROM with current observation headers')
        return row[0]
    profile=symbol('trial_runtime',44*4);book=symbol('witch_book_runtime',22*4)
    native_env=dict(os.environ)
    if args.special or args.special_restores:native_env['MANOSABA_TRIAL_SPECIAL_PROFILE']=symbol('trial_special_runtime',16*4)
    if args.special_restores:
        native_env['MANOSABA_TRIAL_MAP_SAVE']=native_env['MANOSABA_TRIAL_PICTURE_OVERLAYS']=symbol('save_runtime',18*4)
    probes={k:v for k,v in native_env.items() if k.startswith('MANOSABA_TRIAL_')}
    # Refuse fixtures from a different story build, even when script IDs match.
    story_source=(GBA/'src/story_data.cpp').read_text()
    encoded=re.search(r'story_bytes\[\] = \{(.*?)\};',story_source,re.S)[1]
    source_bytes=bytes(map(int,re.findall(r'\d+',encoded)))
    byte_symbol=next(line.split() for line in symbols.splitlines() if line.endswith(' _ZN8manosaba11story_bytesE'))
    offset=int(byte_symbol[0],16)-0x08000000
    if rom.read_bytes()[offset:offset+len(source_bytes)]!=source_bytes:
        raise SystemExit('ROM story bytes differ from current fixture source; rebuild or restore matching sources')
    scopes=('segment','chapter') if args.scope=='both' else (args.scope,)
    routes=[]
    for scope in scopes:
        planned,_=planned_routes(scope,args.match)
        routes.extend(planned)
    if args.limit is not None:routes=routes[:args.limit]
    # Keep all matching source lines; repeated wording is not a unique location.
    data=story_source.split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    rows=[row for row in data.splitlines() if row.startswith('{')]
    paths={i:json.loads(row[1:].split(',',1)[0]) for i,row in enumerate(rows)}
    source_index={}
    used={s['script'] for route in routes for s in route['steps']}
    def normalize(text):
        text=html.unescape(re.sub(r'<[^>]*>','',text))
        return ''.join(c for c in text if not c.isspace() and c!='\u2060' and not '\ue000'<=c<='\ue00f')
    for script,path in paths.items():
        if script not in used:continue
        document=GBA.parent/'story/zh-Hans'/f'{path}.json'
        if not document.exists():continue
        index={}
        for event in json.loads(document.read_text())['events']:
            params=event.get('params',{});text=params.get('Text',params.get('ChoiceSummary',''))
            if isinstance(text,str) and text:index.setdefault(normalize(text),[]).append(event['line'])
        source_index[script]=index
    for route in routes:
        for step in route['steps']:
            step['source_path']=paths.get(step['script'],'')
            step['source_lines']=source_index.get(step['script'],{}).get(normalize(step['text']),[])
    fixture=dict(format=1,story_sha256=sha(GBA/'src/story_data.cpp'),rom_sha256=rom_hash,
                 scope=args.scope,goals=dict(segment='first successful next-script boundary',chapter='continuous trial chapter to successful exit'),routes=routes)
    (out/'fixtures.json').write_text(json.dumps(fixture,ensure_ascii=False,indent=2)+'\n')
    selected=routes
    if not selected:raise SystemExit('No trial routes match the requested filter')
    if args.plan_only:
        print(f'Planned {len(routes)} routes: {out/"fixtures.json"}')
        return
    driver=build('trial_walk_test',[GBA/'tools/trial_walk_test.c'])
    probe=build('trial_walk_probe_test',[GBA/'tools/trial_walk_probe_test.c'],flags=('-O2','-Wall','-Wextra','-Wno-unused-function'))
    probe_out=out/'probe';probe_out.mkdir(exist_ok=True)
    control=subprocess.run([str(probe),str(rom),str(probe_out),profile,book],capture_output=True,text=True)
    (out/'probe-test.log').write_text(control.stdout+control.stderr)
    if control.returncode:raise SystemExit('OBJ detector control failed; see probe-test.log')
    print(f'Routes: {sum(r["solved"] for r in routes)}/{len(routes)} planned; running {len(selected)} with {args.jobs} workers',flush=True)
    def execute(route):
        directory=out/(route['scope']+'--'+route['path'].replace('/','--'));directory.mkdir(exist_ok=True)
        if not route['solved']:return dict(path=route['path'],status='route_unresolved')
        plan=directory/'plan.txt';steps=route['steps'];terminal=dict(route['terminal'],action=0,arg=0,kind=0)
        plan.write_text(f'{route["offset"]} {len(steps)}\n'+''.join(' '.join(str(s[k]) for k in ('script','pc','state','action','arg','kind'))+'\n' for s in steps+[terminal]))
        start=time.monotonic()
        command=[str(driver),str(rom),str(directory),profile,book,str(plan),str(args.max_frames)]
        try:
            native=subprocess.run(command,capture_output=True,text=True,timeout=args.timeout,env=native_env)
            (directory/'run.log').write_text(native.stdout+native.stderr)
            lines=[s for s in native.stdout.splitlines() if s.startswith('{')]
            result=json.loads(lines[-1]) if lines else dict(status='native_failure',returncode=native.returncode)
            if native.returncode and result.get('status')=='passed':result['status']='native_failure'
        except subprocess.TimeoutExpired as error:
            result=dict(status='host_timeout');(directory/'run.log').write_bytes((error.stdout or b'')+(error.stderr or b''))
        result.update(path=route['path'],scope=route['scope'],seconds=round(time.monotonic()-start,3),directory=str(directory.relative_to(out)))
        if 'completed' in result and result['completed']<len(steps):result['checkpoint']=steps[result['completed']]
        try:
            from PIL import Image
        except ImportError:
            Image=None
        if Image:
            for picture in directory.glob('*.ppm'):
                Image.open(picture).save(picture.with_suffix('.png'))
        (directory/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        return result
    driver_hash=file_hash(driver)
    def run(route):
        signature=digest(dict(rom=rom_hash,driver=driver_hash,profile=profile,book=book,route=route,
            max_frames=args.max_frames,probes=probes))
        directory=CACHE/'trial-results'/signature
        with locked(directory/'result.lock'):
            saved=directory/'result.json'
            if not args.fresh and saved.exists():
                result=json.loads(saved.read_text())
                return dict(result,cached=True,seconds=0,directory=str(directory))
            result=execute(route);result.update(cached=False,cache_signature=signature)
            if result['status']=='passed' and not result.get('missing_cases'):
                source=out/result['directory']
                for name in ('plan.txt','events.jsonl','end.ppm','end.png','run.log'):
                    if (source/name).exists():shutil.copy2(source/name,directory/name)
                saved.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
            return result
    results=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures=[pool.submit(run,r) for r in sorted(selected,key=lambda r:len(r['steps']),reverse=True)]
        for task in as_completed(futures):
            result=task.result();results.append(result)
            print(f'{len(results)}/{len(selected)} {result["status"]}{" (cached)" if result.get("cached") else ""}: {result["path"]}; steps={result.get("completed",0)} frames={result.get("frames",0)} gaps={result.get("missing_cases",0)}',flush=True)
            (out/'progress.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
    results.sort(key=lambda r:(r.get('scope',''),r['path']))
    failures=[r for r in results if r['status']!='passed' or r.get('missing_cases',0)]
    report=dict(rom_sha256=rom_hash,elf_sha256=sha(frozen_elf),fixture_story_sha256=fixture['story_sha256'],probe_passed=True,
                scope=args.scope,routes=len(results),passed=sum(r['status']=='passed' for r in results),
                visual_failures=sum(bool(r.get('missing_cases')) for r in results),
                reused_routes=sum(bool(r.get('cached')) for r in results),executed_routes=sum(not r.get('cached') for r in results),
                executed_frames=sum(r.get('frames',0) for r in results if not r.get('cached')),wall_seconds=round(time.monotonic()-started,3),
                frames=sum(r.get('frames',0) for r in results),results=results)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    (out/'failures.json').write_text(json.dumps(failures,ensure_ascii=False,indent=2)+'\n')
    (out/'issues.json').write_text(json.dumps(summarize_issues(results,out),ensure_ascii=False,indent=2)+'\n')
    (out/'performance.json').write_text(json.dumps([r for r in results if r.get('missed',0)],ensure_ascii=False,indent=2)+'\n')
    print(f'Finished: {report["passed"]}/{report["routes"]} routes, {report["visual_failures"]} with missing pixels, {report["frames"]} native frames; {out/"report.json"}',flush=True)
    if failures:raise SystemExit(1)

if __name__=='__main__':main()
