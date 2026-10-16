#!/usr/bin/env python3
"""Source delivery uses exact Git blobs, never mutable checkout contents."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

TREE = Path(__file__).resolve().parents[1]
SCRIPT = TREE / 'tools/prepare_divinus_source.py'
CHECKOUT = os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')


class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SCRIPT.is_file():
            raise AssertionError('approved source helper is missing')
        spec = importlib.util.spec_from_file_location('prepare_divinus', SCRIPT)
        cls.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.helper)
        if not CHECKOUT:
            raise AssertionError('set NT98513_TEST_DIVINUS_CHECKOUT to the pinned public checkout')
        cls.checkout = Path(CHECKOUT)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='nt98513-source-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.out = self.root / 'cache'

    def prepare(self):
        return self.helper.prepare(self.checkout, self.out)

    def test_exact_archive_and_idempotence(self):
        archive = self.prepare()
        manifest = self.helper.load_manifest()
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), manifest['sha256'])
        with tarfile.open(archive) as tf:
            members = tf.getmembers()
            self.assertEqual(len(members), 48)
            prefix = archive.name.removesuffix('.tar.gz') + '/'
            self.assertEqual({m.name.removeprefix(prefix) for m in members}, set(manifest['files']))
            for member in members:
                self.assertTrue(member.isfile())
                self.assertEqual((member.mode, member.uid, member.gid, member.mtime), (0o644, 0, 0, 0))
                self.assertEqual(hashlib.sha256(tf.extractfile(member).read()).hexdigest(), manifest['files'][member.name.removeprefix(prefix)])
        before = archive.stat()
        self.assertEqual(self.prepare(), archive)
        self.assertEqual(archive.stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual(archive.stat().st_ino, before.st_ino)

    def test_mismatched_blob_never_publishes(self):
        real = self.helper.read_blob
        for name in ('src/main.c', 'LICENSE', 'docs/owned-input.md'):
            with self.subTest(name=name), patch.object(self.helper, 'read_blob', side_effect=lambda repo, commit, path: real(repo, commit, path) + (b'x' if path == name else b'')):
                with self.assertRaises(ValueError):
                    self.prepare()
                self.assertFalse(self.out.exists())

    def test_missing_commit_or_member_never_publishes(self):
        empty = self.root / 'empty'
        subprocess.run(['git', 'init', '-q', str(empty)], check=True)
        with self.assertRaises(ValueError):
            self.helper.prepare(empty, self.out)
        self.assertFalse(self.out.exists())
        real = self.helper.read_blob
        def missing(repo, commit, path):
            if path == 'src/main.c':
                raise ValueError('missing blob')
            return real(repo, commit, path)
        with patch.object(self.helper, 'read_blob', side_effect=missing), self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.out.exists())

    def test_existing_wrong_output_preserved(self):
        archive = self.prepare()
        archive.write_bytes(b'preserve me')
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(archive.read_bytes(), b'preserve me')

    def test_symlink_output_components_and_archive_refused(self):
        target = self.root / 'real'
        target.mkdir()
        self.out.symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(list(target.iterdir()), [])
        self.out.unlink()
        archive = self.prepare()
        archive.unlink()
        victim = self.root / 'victim'; victim.write_bytes(b'untouched')
        archive.symlink_to(victim)
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(victim.read_bytes(), b'untouched')

    def test_unsafe_manifest_members_refused(self):
        for name in ('../outside', '/absolute', 'src/../../outside', 'src//main.c', 'src/main.c\n', 'src\\main.c'):
            with self.subTest(name=name):
                m = self.helper.load_manifest()
                m['files'][name] = m['files'].pop('src/main.c')
                m['closure'][m['closure'].index('src/main.c')] = name
                with patch.object(self.helper, 'load_manifest', return_value=m), self.assertRaisesRegex(ValueError, 'unsafe source member|invalid source closure'):
                    self.prepare()
                self.assertFalse(self.out.exists())

    def test_build_output_cannot_reuse_console_or_nonempty_unknown_tree(self):
        self.prepare()
        out = self.root / 'build'
        self.helper.verify(self.out, out)
        out.mkdir(); (out / '.config').write_text('# console\n')
        with self.assertRaises(ValueError):
            self.helper.verify(self.out, out)
        (out / '.config').write_text('BR2_PACKAGE_DIVINUS_OWNED_NT98513=y\n')
        self.helper.verify(self.out, out)
        (out / '.config').unlink(); (out / 'unknown').touch()
        with self.assertRaises(ValueError):
            self.helper.verify(self.out, out)


if __name__ == '__main__':
    unittest.main()
