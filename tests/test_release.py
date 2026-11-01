import hashlib
import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'deploy'))
from package_release import identity
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
