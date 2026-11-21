"""Original-color source coverage, VM save roundtrips and parallel native UI checks."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from PIL import Image
from host_test_cache import GBA,build,planned_routes
from story_text_color import color_spans
from trial_ui import clock_frame

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    rom=args.rom.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    from story import Compiler,load_documents
    documents,_=load_documents();compiler=Compiler(documents)
    for document in documents:compiler.compile(document)
    compiler.link();styles=compiler.text_colors
    colored_source = set()
    for path in (GBA.parent/'story/zh-Hans').rglob('*.json'):
        doc=json.loads(path.read_text())
        for event in doc.get('events',[]):
            if event['op'] in ('PrintTextModified','PrintDebateText') and '<color=' in event.get('params',{}).get('Text','').lower():
                colored_source.add((doc['path'],event['line']))
    assert colored_source == {(s['path'],s['line']) for s in styles}
    # Re-check every source event and its final layout, rather than testing only samples.
    for style in styles:
        doc=json.loads((GBA.parent/'story/zh-Hans'/(style['path']+'.json')).read_text())
        event=next(e for e in doc['events'] if e['line']==style['line'])
        assert color_spans(event['params']['Text'],style['text'])==(style['color'],style['spans'])
    clock,_=clock_frame();assert not set((3,4,5)) & set(clock.tobytes())
    host=build('dialogue_color_test',[GBA/x for x in ('tools/dialogue_color_test.cpp','src/story_vm.cpp','src/lzss.cpp','src/story_data.cpp','src/story_text_color.cpp')],compiler='g++',flags=('-std=c++17','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(GBA/'src')),libraries=())
    result=subprocess.run([str(host)],capture_output=True,text=True);print(result.stdout+result.stderr,flush=True);(out/'vm.log').write_text(result.stdout+result.stderr);assert result.returncode==0
    nm=subprocess.check_output(['/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
    address=lambda name:next(r.split()[0] for r in nm.splitlines() if r.endswith(' '+name))
    addresses=[address(n) for n in ('trial_runtime','dialogue_history_runtime','witch_book_runtime','save_runtime')]
    data=(GBA/'src/story_data.cpp').read_text();rows=data.split('extern const story_script story_scripts[] = {')[1].split('};')[0]
    paths=[json.loads(row[1:].split(',',1)[0]) for row in rows.splitlines() if row.startswith('{')]
    import re
    first=int(re.search(r'story_trial_first = (\d+)',data)[1])
    cases=[('Act01_Chapter01/Act01_Chapter01_Adv13',57),('Act01_Chapter01/Act01_Chapter01_Bad03',60),('Act01_Chapter01/Act01_Chapter01_Bad03',132),('Act01_Chapter02/Act01_Chapter02_Trial14',103),('Act02_Chapter06/Act02_Chapter06_Trial00',93),('Act02_Chapter06/Act02_Chapter06_Trial04',564)]
    routes={}
    for path,_ in cases:
        if '_Trial' in path and path not in routes:
            planned,_=planned_routes('segment',[path]);routes[path]=next(r for r in planned if r['path']==path)
    native=build('dialogue_color_game_test',[GBA/'tools/dialogue_color_game_test.c'])
    def run(case):
        path,line=case;style=next(r for r in styles if r['path']==path and r['line']==line)
        directory=out/(path.split('/')[-1]+'-'+str(line));directory.mkdir(exist_ok=True);routefile=directory/'route.txt';script=paths.index(path);trial='_Trial' in path
        if trial:
            steps=routes[path]['steps']
            if not any(s['pc']==style['end'] for s in steps):steps=[] # Capture a later statement before taking an objection.
        else:steps=[]
        routefile.write_text(''.join(f"{s['script']} {s['pc']} {s['state']} {s['action']} {s['arg']}\n" for s in steps))
        command=[str(native),str(rom),str(directory),*addresses,str(script-first if trial else script),str(int(trial)),str(script),str(style['end']),str(style['color']),str(routefile)]
        env=dict(os.environ)
        if style["text"].count("\n")>2 and not trial:env["MANOSABA_COLOR_SECOND_PAGE"]="1"
        run=subprocess.run(command,capture_output=True,text=True,timeout=240,env=env);log=run.stdout+run.stderr;(directory/'run.log').write_text(log)
        for image in directory.glob('*.ppm'):Image.open(image).save(image.with_suffix('.png'))
        print(path,line,log,flush=True);return dict(path=path,line=line,passed=run.returncode==0,log=log)
    with ThreadPoolExecutor(max_workers=6) as pool:results=list(pool.map(run,cases))
    summary=dict(rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),source_utterances=len(styles),source_spans=sum(len(r['spans']) for r in styles),clock_reserved_ink_free=True,host_passed=True,cases=results,passed=all(r['passed'] for r in results))
    (out/'report.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    if not summary['passed']:raise SystemExit(1)
if __name__=='__main__':main()
