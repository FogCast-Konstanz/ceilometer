"""Pull a tested deployment branch; apply only while confirmed disabled."""
import argparse
import json
import os
import subprocess
import sys
sys.dont_write_bytecode=True
import tempfile
from pathlib import Path
from install_release import CURRENT,deploy,health

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository',default=os.environ.get('CEILOMETER_GIT_URL'))
    parser.add_argument('--ref',default=os.environ.get('CEILOMETER_GIT_REF','refs/heads/deploy'))
    parser.add_argument('--pico-serial',default=os.environ.get('CEILOMETER_PICO_SERIAL'))
    args=parser.parse_args()
    if not args.repository or not args.pico_serial: raise RuntimeError('Configure Git URL and expected Pico serial first')
    if not args.ref.startswith('refs/heads/'): raise ValueError('Track an explicit deployment branch ref')
    os.environ['GIT_TERMINAL_PROMPT']='0'
    os.environ['GIT_SSH_COMMAND']='ssh -o BatchMode=yes -o StrictHostKeyChecking=yes'
    status=health()
    if status['enabled'] or not status['connected'] or not status['stop_confirmed']:
        print('DEFERRED: instrument is active or shutdown is unconfirmed');return
    repo=Path('/var/lib/ceilometer-cd/repo');repo.mkdir(parents=True,exist_ok=True)
    def git(*parts,**kwargs): return subprocess.run(['git','-C',str(repo),*parts],check=True,**kwargs)
    if not (repo/'.git').exists(): git('init')
    git('fetch','--depth=1',args.repository,args.ref,timeout=90)
    manifest=json.loads(git('show','FETCH_HEAD:release.json',capture_output=True,text=True).stdout)
    if CURRENT.is_symlink() and CURRENT.resolve().name==manifest['release_id']:
        print('ALREADY_CURRENT '+manifest['release_id']);return
    with tempfile.TemporaryDirectory(prefix='ceilometer-update-') as directory:
        archive=Path(directory)/'release.tar'
        with archive.open('wb') as file: git('archive','--format=tar','FETCH_HEAD',stdout=file)
        # deploy rechecks disabled status immediately before stopping anything.
        deploy(archive,args.pico_serial)
if __name__=='__main__': main()
