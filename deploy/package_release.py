"""Create a self-contained runtime + Pico UF2 release without credentials or data."""
import argparse
import hashlib
import json
import tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DIRECTORIES=('tools','gui','pi-dashboard','deploy','tests','src','include','docs')
ROOT_FILES=('CMakeLists.txt','README.md','.gitignore')
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def identity(manifest):
    return hashlib.sha256(json.dumps({k:manifest[k] for k in ('schema','source_revision','target','files')},sort_keys=True,separators=(',',':')).encode()).hexdigest()[:24]
def package(output,firmware,revision):
    paths=[]
    for directory in DIRECTORIES:
        for path in (ROOT/directory).rglob('*'):
            if not path.is_file() or path.is_symlink() or '__pycache__' in path.parts or path.suffix in ('.pyc','.env','.pem','.key'): continue
            paths.append(path)
    paths.extend(ROOT/name for name in ROOT_FILES)
    files={p.relative_to(ROOT).as_posix():digest(p) for p in sorted(paths)}
    files['firmware/ceilometer.uf2']=digest(firmware)
    manifest={'schema':1,'source_revision':revision,'target':'pico2','files':files,
              'created_utc':datetime.now(timezone.utc).isoformat()}
    manifest['release_id']=identity(manifest)
    encoded=(json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode()
    import io
    with tarfile.open(output,'w:gz') as archive:
        for path in paths: archive.add(path,arcname=path.relative_to(ROOT).as_posix(),recursive=False)
        archive.add(firmware,arcname='firmware/ceilometer.uf2',recursive=False)
        info=tarfile.TarInfo('release.json');info.size=len(encoded);info.mode=0o644;archive.addfile(info,io.BytesIO(encoded))
    print(json.dumps({'release_id':manifest['release_id'],'archive':str(output),'sha256':digest(Path(output))}))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,default=ROOT/'build/ceilometer.uf2')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--revision',default='workspace-snapshot')
    args=parser.parse_args();package(args.output,args.firmware,args.revision)
