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

def release_available(repository,ref):
    result=subprocess.run(['git','ls-remote','--exit-code','--heads',repository,ref],
                          capture_output=True,text=True,timeout=90)
    if result.returncode==2:
        print('WAITING: no published deployment branch yet');return False
    result.check_returncode()
    return True

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
    if not release_available(args.repository,args.ref): return
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
