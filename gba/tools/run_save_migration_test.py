"""Mount an existing Flash save in both ROM backends, upgrade, and cold-load v5."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,subprocess,json
from host_test_cache import GBA,build

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('flash_rom',type=Path);p.add_argument('sram_rom',type=Path)
    p.add_argument('input_save',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    converted=out/'converted.sav'
    node="""
const fs=require('fs'),vm=require('vm');
const html=fs.readFileSync(process.argv[1],'utf8');
vm.runInThisContext(html.split('<script>')[1].split('const file=document.')[0]);
const parsed=SaveConverter.parse(new Uint8Array(fs.readFileSync(process.argv[2])));
fs.writeFileSync(process.argv[3],SaveConverter.convert(parsed,[0,1,2,3,4,5,6,7]));
"""
    subprocess.run(['node','-e',node,str(GBA/'tools/save-converter.html'),str(args.input_save.resolve()),str(converted)],check=True)
    def run(spec):
        name,rom,source,sram=spec;directory=out/name;directory.mkdir(exist_ok=True);rom=rom.resolve()
        test=build('save_migration_game_test',[GBA/'tools/save_migration_game_test.c'],flags=('-O2','-Wall','-Wextra','-DMANOSABA_TEST_SRAM='+str(int(sram))))
        symbols=subprocess.check_output(['arm-none-eabi-nm',str(rom.with_suffix('.elf'))],text=True)
        def address(name):return next(row.split()[0] for row in symbols.splitlines() if row.endswith(' '+name))
        with (directory/'native.log').open('w') as log:
            result=subprocess.run([str(test),str(rom),str(directory),address('save_runtime'),address('trial_runtime'),address('_ZN8manosaba20graphics_performanceE'),str(source.resolve())],stdout=log,stderr=subprocess.STDOUT,timeout=120)
        print(name,':','PASS' if not result.returncode else 'FAIL',flush=True)
        return dict(name=name,passed=result.returncode==0)
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=list(pool.map(run,[('flash',args.flash_rom,args.input_save,False),('sram',args.sram_rom,converted,True)]))
    (out/'report.json').write_text(json.dumps(results,indent=2)+'\n')
    if not all(r['passed'] for r in results):raise SystemExit(1)

if __name__=='__main__':main()
