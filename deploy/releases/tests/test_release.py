import hashlib
import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from package_release import identity,package
from install_release import verify,extract,normalize_permissions
import os
import stat
class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        (self.root/'file.py').write_text('print("test")\n')
        self.manifest=dict(schema=1,source_revision='test',target='pico2',files={'file.py':hashlib.sha256((self.root/'file.py').read_bytes()).hexdigest()})
        self.manifest['release_id']=identity(self.manifest);self.save()
    def save(self): (self.root/'release.json').write_text(json.dumps(self.manifest))
    def tearDown(self): self.temp.cleanup()
    def test_runtime_package_boundaries_and_legacy_bridge(self):
        firmware=self.root/'test.uf2';firmware.write_bytes(b'test firmware')
        archive=self.root/'release.tar.gz';package(archive,firmware,'test')
        stage=self.root/'unpacked';stage.mkdir();manifest=extract(archive,stage)
        names=set(manifest['files'])
        self.assertIn('software/instrument/src/fogcast/web/static/index.html',names)
        self.assertIn('software/instrument/src/fogcast/web/static/vendor/webglplot.umd.js',names)
        self.assertIn('tools/ceilometer.py',names)
        self.assertIn('tests/test_dashboard.py',names)
        self.assertFalse(any(n.startswith(('hardware/','software/bringup/','firmware/pico2/')) for n in names))
        self.assertEqual({n for n in names if n.startswith('firmware/')},{'firmware/ceilometer.uf2'})
        compile((stage/'tools/ceilometer.py').read_text(),'legacy USB bridge','exec')
        compile((stage/'tests/test_dashboard.py').read_text(),'legacy test bridge','exec')
    def test_inventory_and_identity(self):
        self.assertEqual(verify(self.root)['release_id'],self.manifest['release_id'])
        (self.root/'unverified.py').write_text('extra')
        with self.assertRaises(ValueError): verify(self.root)
    @unittest.skipUnless(os.name=='posix','POSIX service permissions')
    def test_release_permissions(self):
        self.root.chmod(0o700)
        normalize_permissions(self.root)
        self.assertEqual(stat.S_IMODE(self.root.stat().st_mode),0o755)
        self.assertEqual(stat.S_IMODE((self.root/'file.py').stat().st_mode),0o644)
    def test_corruption(self):
        (self.root/'file.py').write_text('corrupt')
        with self.assertRaises(ValueError): verify(self.root)
    def test_target(self):
        self.manifest['target']='other';self.save()
        with self.assertRaises(ValueError): verify(self.root)
    def test_archive_traversal(self):
        bundle=self.root/'bad.tar'
        with tarfile.open(bundle,'w') as archive:
            info=tarfile.TarInfo('../escape');info.size=1;archive.addfile(info,io.BytesIO(b'x'))
        destination=self.root/'stage';destination.mkdir()
        with self.assertRaises(ValueError): extract(bundle,destination)
    def test_archive_symlink(self):
        bundle=self.root/'bad.tar'
        with tarfile.open(bundle,'w') as archive:
            info=tarfile.TarInfo('link');info.type=tarfile.SYMTYPE;info.linkname='/etc';archive.addfile(info)
        destination=self.root/'stage';destination.mkdir()
        with self.assertRaises(ValueError): extract(bundle,destination)
if __name__=='__main__': unittest.main()
