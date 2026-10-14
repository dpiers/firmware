#!/usr/bin/env python3
"""Independent filesystem checks of the narrowly scoped rootfs normalization."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

script = Path(__file__).resolve().parents[1] / 'board/normalize-rootfs.py'
source = script.read_bytes()
rows = []
with tempfile.TemporaryDirectory(prefix='nt98513-normalization-root-') as tmp:
    base = Path(tmp)
    def setup(name, config='BR2_STATIC_LIBS=y\n'):
        out = base / name
        (out / 'target/lib').mkdir(parents=True)
        (out / 'images').mkdir()
        (out / '.config').write_text(config)
        return out
    def run(out, label, success):
        env = dict(os.environ, BR2_CONFIG=str(out / '.config'), BINARIES_DIR=str(out / 'images'))
        p = subprocess.run([sys.executable, str(script), str(out / 'target')], env=env,
                           capture_output=True, text=True, timeout=10)
        assert (p.returncode == 0) == success, (label, p.stderr)
        rows.append({'case': label, 'exit': p.returncode, 'stdout': p.stdout,
                     'stderr': p.stderr, 'expected_success': success})
    out = setup('accepted')
    link = out / 'target/lib/ld-musl*'
    link.symlink_to('libc.so')
    inode = link.lstat().st_ino
    run(out, 'move exact generated link', True)
    saved = out / 'images/unused-static-musl-loader-link'
    receipt = out / 'images/static-musl-normalization.json'
    assert not os.path.lexists(link) and saved.is_symlink()
    assert saved.lstat().st_ino == inode and os.readlink(saved) == 'libc.so'
    initial_receipt = receipt.read_bytes()
    assert json.loads(initial_receipt)['original']['rootfs_path'] == '/lib/ld-musl*'
    run(out, 'repeat without regenerating link', True)
    link.symlink_to('libc.so')
    run(out, 'remove only regenerated identical duplicate', True)
    assert not os.path.lexists(link) and saved.lstat().st_ino == inode
    assert receipt.read_bytes() == initial_receipt
    for name, kind in [('regular', 'regular'), ('foreign-target', 'link')]:
        out = setup(name)
        path = out / 'target/lib/ld-musl*'
        if kind == 'regular': path.write_bytes(b'keep this exact file\n')
        else: path.symlink_to('different-loader.so')
        before = path.lstat()
        run(out, 'preserve unexpected ' + name, False)
        assert path.lstat().st_ino == before.st_ino
        if kind == 'regular': assert path.read_bytes() == b'keep this exact file\n'
        else: assert os.readlink(path) == 'different-loader.so'
        assert not list((out / 'images').iterdir())
    for name, config in [('dynamic-libc', 'BR2_STATIC_LIBS=y\n'), ('nonstatic-config', '# BR2_STATIC_LIBS is not set\n')]:
        out = setup(name, config)
        path = out / 'target/lib/ld-musl*'
        path.symlink_to('libc.so')
        if name == 'dynamic-libc': (out / 'target/lib/libc.so').write_bytes(b'preserve libc\n')
        run(out, 'reject ' + name, False)
        assert path.is_symlink() and os.readlink(path) == 'libc.so'
        assert not list((out / 'images').iterdir())
        if name == 'dynamic-libc': assert (out / 'target/lib/libc.so').read_bytes() == b'preserve libc\n'
assert script.read_bytes() == source
print('PASS: seven static-musl filesystem guard cases; no target execution')
