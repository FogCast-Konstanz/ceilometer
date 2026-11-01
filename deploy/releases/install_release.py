"""Root-side transactional Pi deployment. Reboots/updates always remain disabled."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from pathlib import Path
sys.dont_write_bytecode=True
from package_release import identity
RELEASES=Path('/opt/ceilometer/releases')
CURRENT=Path('/opt/ceilometer-firmware')
ENV=Path('/etc/ceilometer/dashboard.env')
SERVICE='ceilometer-dashboard'
def run(*args,**kwargs): return subprocess.run(args,check=True,text=True,**kwargs)
def settings():
    result={}
    for line in ENV.read_text().splitlines():
        if line and not line.startswith('#') and '=' in line:
            k,v=line.split('=',1);result[k]=v.strip().strip('"\'')
    return result

def health():
    token=settings().get('CEILOMETER_WEB_TOKEN','')
    request=urllib.request.Request('http://127.0.0.1:8080/api/status',headers={'Authorization':'Bearer '+token})
    with urllib.request.urlopen(request,timeout=3) as response: return json.load(response)
def verify(root):
    manifest=json.loads((root/'release.json').read_text())
    if manifest.get('schema')!=1 or manifest.get('target')!='pico2' or manifest.get('release_id')!=identity(manifest):
        raise ValueError('Release manifest identity/target mismatch')
    actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and p!=root/'release.json'}
    if actual!=set(manifest['files']) or any(p.is_symlink() for p in root.rglob('*')): raise ValueError('Release file inventory mismatch')
    for name,expected in manifest['files'].items():
        path=(root/name).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file() or (root/name).is_symlink(): raise ValueError('Unsafe release file')
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected: raise ValueError('Release checksum mismatch: '+name)
    return manifest

def extract(archive,destination):
    with tarfile.open(archive,'r:*') as bundle:
        for entry in bundle.getmembers():
            if not (entry.isfile() or entry.isdir()) or not (destination/entry.name).resolve().is_relative_to(destination.resolve()):
                raise ValueError('Release contains unsafe archive entries')
        bundle.extractall(destination,filter='data')
    return verify(destination)
def normalize_permissions(root):
    root.chmod(0o755)
    for path in root.rglob('*'):
        path.chmod(0o755 if path.is_dir() else 0o644)
def install_units(release):
    for name in ('ceilometer-dashboard.service','ceilometer-update.service','ceilometer-update.timer'):
        destination=Path('/etc/systemd/system')/name
        shutil.copyfile(release/('deploy/raspberry-pi' if (release/'deploy/raspberry-pi').exists() else 'deploy')/name,destination);destination.chmod(0o644)
    run('systemctl','daemon-reload')
def point_to(release):
    temporary=CURRENT.with_name('ceilometer-firmware.next')
    if temporary.is_symlink(): temporary.unlink()
    temporary.symlink_to(release);temporary.replace(CURRENT)
def flash(release,serial):
    run('picotool','load','-v','-x',str(release/'firmware/ceilometer.uf2'),'--ser',serial,'-f',timeout=60)
    time.sleep(2)
def wait_healthy(expected_release=None):
    deadline=time.monotonic()+40
    while time.monotonic()<deadline:
        try:
            value=health()
            if (value['connected'] and not value['enabled'] and value['stop_confirmed'] and
                value.get('firmware') and value.get('configuration') and
                (expected_release is None or value.get('deployment',{}).get('release_id')==expected_release)): return
        except Exception: pass
        time.sleep(1)
    raise RuntimeError('Dashboard/USB did not confirm disabled outputs after deployment')
def deploy(archive,serial,initial=False):
    import fcntl
    if os.geteuid()!=0: raise PermissionError('Run installer with sudo')
    lock=open('/run/lock/ceilometer-update.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    if CURRENT.exists() and not CURRENT.is_symlink(): raise RuntimeError('Existing non-release installation requires explicit migration')
    previous=CURRENT.resolve() if CURRENT.is_symlink() else None
    if initial and previous: raise RuntimeError('Initial bare-board provisioning is only allowed without a previous release')
    if not initial:
        status=health()
        if status['enabled'] or not status['stop_confirmed'] or not status['connected']:
            print('DEFERRED: disable the instrument and wait for board acknowledgement');return False
    RELEASES.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='staging-',dir=RELEASES))
    manifest=extract(archive,stage);target=RELEASES/manifest['release_id']
    # mkdtemp starts at 0700; published releases must be readable/traversable
    # by the unprivileged dashboard account, without allowing writes.
    normalize_permissions(stage)
    if previous==target:
        shutil.rmtree(stage)
        print('ALREADY_CURRENT '+manifest['release_id']);return True
    # Exercise the actual Linux/Pi host runtime before stopping the old service.
    for test in ('deploy/releases/tests/test_release.py','deploy/releases/tests/test_updater.py','software/instrument/tests/test_stream_client.py','software/instrument/tests/test_dashboard.py'):
        run('/usr/bin/python3',str(stage/test),cwd=stage,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'},timeout=120)
    if target.exists(): verify(target);shutil.rmtree(stage)
    else: stage.rename(target)
    old_hash=None
    if previous: old_hash=verify(previous)['files'].get('firmware/ceilometer.uf2')
    run('systemctl','stop',SERVICE)
    try:
        # A second serial Stop is required after the service has released USB.
        # Only initial provisioning of the explicitly selected bare board bypasses this.
        if not initial:
            sys.path.insert(0,str(target/'software/instrument/src'))
            from fogcast.ceilometer import Ceilometer
            device=Ceilometer(settings()['CEILOMETER_SERIAL'])
            try:
                device.command('stop')
                state=device.command('get_state');tec=device.command('get_temperature_control');bias=device.command('get_bias')
                if state[5]!='0' or state[6]!='0' or tec[0]!='0' or bias[0]!='0': raise RuntimeError('Shutdown verification failed')
            finally: device.serial.close()
        if old_hash!=manifest['files']['firmware/ceilometer.uf2']: flash(target,serial)
        point_to(target);install_units(target)
        run('systemctl','start',SERVICE)
        wait_healthy(manifest['release_id'])
        print('DEPLOYED '+manifest['release_id']);return True
    except Exception:
        run('systemctl','stop',SERVICE)
        if previous:
            # A previous release always starts disabled as well.
            if old_hash!=manifest['files']['firmware/ceilometer.uf2']: flash(previous,serial)
            point_to(previous);install_units(previous);run('systemctl','start',SERVICE);wait_healthy(previous.name)
            print('ROLLED_BACK '+previous.name)
        raise
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    parser.add_argument('--pico-serial',required=True)
    parser.add_argument('--initial-bare-board',action='store_true')
    args=parser.parse_args();deploy(args.archive,args.pico_serial,args.initial_bare_board)
