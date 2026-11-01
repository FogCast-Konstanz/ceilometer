"""Create a self-contained runtime + Pico UF2 release without credentials or data."""
import argparse
import hashlib
import json
import tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DIRECTORIES=('software/instrument/src','software/instrument/tests','deploy/releases','deploy/raspberry-pi','interfaces')
ROOT_FILES=('README.md','software/instrument/pyproject.toml','software/instrument/requirements.txt')
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def identity(manifest):
    return hashlib.sha256(json.dumps({k:manifest[k] for k in ('schema','source_revision','target','files')},sort_keys=True,separators=(',',':')).encode()).hexdigest()[:24]
def package(output,firmware,revision):
    paths=[]
    for directory in DIRECTORIES:
        for path in (ROOT/directory).rglob('*'):
            if not path.is_file() or path.is_symlink() or any(x=='__pycache__' or x.endswith('.egg-info') for x in path.parts) or path.suffix in ('.pyc','.env','.pem','.key'): continue
            paths.append(path)
    paths.extend(ROOT/name for name in ROOT_FILES)
    files={p.relative_to(ROOT).as_posix():digest(p) for p in sorted(paths)}
    files['firmware/ceilometer.uf2']=digest(firmware)
    # Bridge the previously deployed installer: its preflight and unit-copy paths
    # remain available for a one-time transition, without shipping CAD or firmware sources.
    aliases={}
    for name in ('ceilometer-dashboard.service','ceilometer-update.service','ceilometer-update.timer'):
        aliases['deploy/'+name]=(ROOT/'deploy/raspberry-pi'/name).read_bytes()
    for name,actual in (('test_release.py','deploy/releases/tests/test_release.py'),
                        ('test_stream_client.py','software/instrument/tests/test_stream_client.py'),
                        ('test_dashboard.py','software/instrument/tests/test_dashboard.py')):
        aliases['tests/'+name]=('import runpy\nfrom pathlib import Path\nrunpy.run_path(str(Path(__file__).resolve().parents[1]/'+repr(actual)+'),run_name="__main__")\n').encode()
    aliases['tools/ceilometer.py']=b'from pathlib import Path\nimport sys\nsys.path.insert(0,str(Path(__file__).resolve().parents[1]/"software/instrument/src"))\nfrom fogcast.ceilometer import Ceilometer\n'
    files.update({name:hashlib.sha256(data).hexdigest() for name,data in aliases.items()})
    manifest={'schema':1,'source_revision':revision,'target':'pico2','files':files,
              'created_utc':datetime.now(timezone.utc).isoformat()}
    manifest['release_id']=identity(manifest)
    encoded=(json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode()
    import io
    with tarfile.open(output,'w:gz') as archive:
        for path in paths: archive.add(path,arcname=path.relative_to(ROOT).as_posix(),recursive=False)
        archive.add(firmware,arcname='firmware/ceilometer.uf2',recursive=False)
        for name,data in aliases.items():
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o644;archive.addfile(info,io.BytesIO(data))
        info=tarfile.TarInfo('release.json');info.size=len(encoded);info.mode=0o644;archive.addfile(info,io.BytesIO(encoded))
    print(json.dumps({'release_id':manifest['release_id'],'archive':str(output),'sha256':digest(Path(output))}))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,default=ROOT/'build/firmware/ceilometer.uf2')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--revision',default='workspace-snapshot')
    args=parser.parse_args();package(args.output,args.firmware,args.revision)
