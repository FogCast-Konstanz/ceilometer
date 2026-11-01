import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from update_from_git import release_available

class Tests(unittest.TestCase):
    def test_missing_branch_waits(self):
        with patch('update_from_git.subprocess.run',return_value=subprocess.CompletedProcess([],2)):
            self.assertFalse(release_available('https://example.invalid/repo.git','refs/heads/deploy'))
    def test_published_branch_is_available(self):
        with patch('update_from_git.subprocess.run',return_value=subprocess.CompletedProcess([],0)):
            self.assertTrue(release_available('https://example.invalid/repo.git','refs/heads/deploy'))
    def test_network_or_auth_failure_is_not_hidden(self):
        with patch('update_from_git.subprocess.run',return_value=subprocess.CompletedProcess([],128)):
            with self.assertRaises(subprocess.CalledProcessError):release_available('https://example.invalid/repo.git','refs/heads/deploy')

if __name__=='__main__':unittest.main()
