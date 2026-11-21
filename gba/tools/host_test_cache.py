"""Content-addressed host tools and test inputs. Nothing here runs on the GBA."""
from contextlib import contextmanager
from pathlib import Path
import fcntl,hashlib,json,os,re,subprocess

GBA=Path(__file__).resolve().parents[1]
CACHE=GBA/'build-butano/test-cache'

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest()

def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def sdk_fingerprint():
    return {str(p):p.stat().st_mtime_ns for p in [Path('/usr/include/mgba/core/core.h'),Path('/usr/lib/x86_64-linux-gnu/libmgba.so')] if p.exists()}

@contextmanager
def locked(path):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('a') as handle:
        fcntl.flock(handle,fcntl.LOCK_EX)
        yield

def dependencies(sources):
    found=set()
    def visit(path):
        path=Path(path).resolve()
        if path in found:return
        found.add(path)
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"',path.read_text(),re.M):
            for candidate in (path.parent/include,GBA/'src'/include,GBA/'tools'/include):
                if candidate.exists():visit(candidate);break
    for source in sources:visit(source)
    return {str(p):file_hash(p) for p in sorted(found)}

def build(name,sources,compiler='cc',flags=('-O2','-Wall','-Wextra'),libraries=('-lmgba',)):
    sources=[Path(p).resolve() for p in sources]
    version=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0]
    # A package update must invalidate native executables, too.
    signature=digest(dict(deps=dependencies(sources),compiler=version,flags=flags,libraries=libraries,sdk=sdk_fingerprint()))
    directory=CACHE/'bin'/signature;target=directory/name
    with locked(directory/'build.lock'):
        if not target.exists():
            directory.mkdir(parents=True,exist_ok=True)
            temporary=directory/(name+'.tmp')
            subprocess.run([compiler,*flags,*map(str,sources),*libraries,'-o',str(temporary)],check=True)
            os.replace(temporary,target)
    return target

def json_cache(namespace,key,producer):
    directory=CACHE/namespace;signature=digest(key);path=directory/(signature+'.json')
    with locked(directory/(signature+'.lock')):
        if path.exists():return json.loads(path.read_text()),True
        result=producer();path.write_text(json.dumps(result,ensure_ascii=False)+'\n')
        return result,False

def planner():
    return build('trial_route_plan',[GBA/'tools/trial_route_plan.cpp',GBA/'src/story_vm.cpp',GBA/'src/lzss.cpp',GBA/'src/story_data.cpp'],
        compiler='g++',flags=('-std=c++17','-O2','-Wall','-Wextra','-I'+str(GBA/'src')),libraries=())

def planned_routes(scope,matches=()):
    executable=planner()
    def produce():
        output=subprocess.check_output([str(executable),'150000',scope,*matches],text=True)
        return [dict(json.loads(row),scope=scope) for row in output.splitlines()]
    return json_cache('trial-routes',dict(tool=str(executable),scope=scope,matches=sorted(matches)),produce)
