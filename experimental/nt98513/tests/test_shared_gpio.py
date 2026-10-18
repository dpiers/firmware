"""Source-only final-chain tests; no Linux Make, compiler or target execution."""
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

EXTERNAL = Path(__file__).resolve().parents[1]
# Only overlay preparation uses a read-only fallback. Normal repository CI needs none.
BASE = Path(os.environ.get('NT98513_OVERLAY_BASE', EXTERNAL))

def read(path):
    p = EXTERNAL / path
    return (p if p.exists() else BASE / path).read_bytes()

def digest(data):
    return hashlib.sha256(data).hexdigest()

def sections(data):
    lines = data.decode().splitlines(True)
    result = {}; i = 0
    while i < len(lines):
        assert lines[i].startswith('--- ')
        old = lines[i][4:].strip(); new = lines[i+1][4:].strip(); i += 2
        assert new.startswith('b/')
        path = new[2:]
        assert path not in result and '..' not in Path(path).parts and not path.startswith('/')
        assert old in ('/dev/null', 'a/' + path)
        hunks = []
        while i < len(lines) and lines[i].startswith('@@ '):
            m = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', lines[i])
            assert m
            a, n, b, k = int(m[1]), int(m[2] or 1), int(m[3]), int(m[4] or 1)
            i += 1; body = []
            while i < len(lines) and not lines[i].startswith(('@@ ', '--- ')):
                assert lines[i][0] in ' +-'
                body.append(lines[i]); i += 1
            assert sum(x[0] in ' -' for x in body) == n
            assert sum(x[0] in ' +' for x in body) == k
            hunks.append((a, n, b, k, body))
        assert hunks
        result[path] = (old, hunks)
    return result

def apply(original, hunks):
    src = original.decode().splitlines(True); out = []; cursor = 0
    for a, _, b, _, body in hunks:
        start = max(a-1, 0)
        assert start >= cursor
        out += src[cursor:start]; cursor = start
        assert len(out) == max(b-1, 0)
        for line in body:
            if line[0] in ' -':
                assert cursor < len(src) and src[cursor] == line[1:]
                cursor += 1
            if line[0] in ' +':
                out.append(line[1:])
    return ''.join(out + src[cursor:]).encode()

MANIFEST = json.loads(read('gpio-cc-source-manifest.json'))
PATCH = read('patches/linux/0004-nt98513-gpio-shared-cc.patch')
# Complete additions of patch3 are available without downloading upstream Linux.
DONOR = {p: apply(b'', h) for p, (old, h) in sections(read('patches/linux/0003-nt98513-shared-clock-board.patch')).items() if old == '/dev/null'}

def final_chain(patch):
    parsed = sections(patch)
    rows = MANIFEST['kernel_files']
    assert len({r['path'] for r in rows}) == len(rows) == 25
    assert set(parsed) == {r['path'] for r in rows}
    partial = set(MANIFEST['partial_upstream_files'])
    assert partial == {'drivers/gpio/Makefile', 'drivers/soc/Makefile'}
    final = dict(DONOR)
    for row in rows:
        path = row['path']; old, hunks = parsed[path]
        if path in partial:
            assert old != '/dev/null' and row['old_sha256']
            additions = [x[1:] for h in hunks for x in h[4] if x[0] == '+']
            symbol = 'GPIO_NT98513' if '/gpio/' in path else 'NT98513_CC_BANK'
            assert additions == ['obj-$(CONFIG_' + symbol + ') += nt98513/\n']
            assert not any(x[0] == '-' for h in hunks for x in h[4])
            continue  # Full upstream bytes/hash must be checked by the real kernel lane.
        original = b'' if old == '/dev/null' else DONOR[path]
        assert row['old_sha256'] == (digest(original) if old != '/dev/null' else None)
        final[path] = apply(original, hunks)
        assert digest(final[path]) == row['sha256']
    return final

GOOD = dict.fromkeys('MACH_NT98513 OF COMMON_CLK WATCHDOG WATCHDOG_HANDLE_BOOT_ENABLED WATCHDOG_NOWAYOUT NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED NT98513_CC_BANK GPIO_NT98513 GPIOLIB OF_GPIO GPIO_CDEV'.split(), 'y')
GOOD['WATCHDOG_OPEN_TIMEOUT'] = '0'
SELECTORS = 'NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED NT98513_CC_BANK GPIO_NT98513'.split()
FORBIDDEN = 'NT98513_WATCHDOG SMP SUSPEND HIBERNATION OF_DYNAMIC GPIO_CDEV_V1 GPIO_SYSFS PINCTRL'.split()

class FinalSource(unittest.TestCase):
    def test_frozen_patch_and_copies(self):
        self.assertEqual(digest(PATCH), MANIFEST['patch4_sha256'])
        final = final_chain(PATCH)
        for r in MANIFEST['copies'] + MANIFEST['unchanged_external_files']:
            data = read(r['path'])
            # Manifest records the accepted GPIO baseline; Ethernet appends new integration.
            if r['path'] in ('targets.mk', 'external.mk'):
                data = data.split(b'# Optional offline Ethernet variant; existing variants above remain unchanged.\n')[0]
            self.assertEqual(digest(data), r['sha256'], r['path'])
        for r in MANIFEST['unchanged_final_imports']:
            self.assertEqual(digest(final[r['kernel']]), r['sha256'])
        # Base/Divinus enable neither the new board nor drivers; package bytes above are unchanged.
        for p in ['board/linux.config', 'board/owned-input-network.config']:
            for s in SELECTORS:
                self.assertNotIn(('CONFIG_'+s+'=y').encode(), read(p))
        self.assertNotIn(b'GPIO_NT98513', read('configs/nt98513_owned_input_defconfig'))

    def test_patch_controls(self):
        for bad in [PATCH + PATCH,
                    PATCH + b'--- /dev/null\n+++ b/unmapped\n@@ -0,0 +1 @@\n+unexpected\n',
                    PATCH.replace(b'unsigned', b'unsignedZ', 1),
                    re.sub(rb'(@@ -\d+,)\d+', rb'\g<1>9999', PATCH, count=1),
                    re.sub(rb'( \+\d+,)\d+', rb'\g<1>9999', PATCH, count=1)]:
            with self.assertRaises((AssertionError, KeyError, IndexError)):
                final_chain(bad)
        parsed = sections(PATCH)
        first = next(iter(parsed))
        start = PATCH.index(b'--- ', 4)
        with self.assertRaises(AssertionError):
            final_chain(PATCH[start:])
        self.assertTrue(first)

    def test_topology(self):
        dt = final_chain(PATCH)['arch/mips/boot/dts/nt98513/nt98513-shared-clock.dts'].decode()
        self.assertEqual(dt.count('compatible = "openipc,nt98513-cc-bank-v1"'), 1)
        self.assertEqual(dt.count('openipc,cc-bank = <&cc_bank>'), 2)
        self.assertEqual(dt.count('<0xc0090050 12>'), 1)
        self.assertIn('reg = <0xc0020074 4>, <0xc0020084 4>;', dt)
        self.assertIn('reg = <0xc0070000 0x10000>, <0xc0030070 4>, <0xc029001c 4>;', dt)
        self.assertIn('openipc,clock-owner = <&clock_owner>', dt)
        self.assertIn('#include "nt98513.dts"', dt)
        self.assertNotIn('gpio-hog', dt)

    def test_actual_shell_guards(self):
        final = final_chain(PATCH)
        with tempfile.TemporaryDirectory(prefix='gpio-guards-') as tmp:
            root = Path(tmp); kernel = root / 'kernel'
            guard = kernel / 'arch/mips/nt98513/check-clock-config.sh'
            guard.parent.mkdir(parents=True)
            guard.write_bytes(final['arch/mips/nt98513/check-clock-config.sh'])
            checker = root / 'expected.sh'; checker.write_bytes(read('tools/check_shared_clock_kernel.sh'))
            config = kernel / '.config'
            def check(values, want, extra=''):
                config.write_text(''.join('CONFIG_%s=%s\n' % p for p in values.items()) + extra)
                p = subprocess.run(['/bin/sh', str(guard), str(config)], capture_output=True)
                self.assertEqual(p.returncode == 0, want, (values, p.stderr))
            def assignments(data):
                result = {}
                for line in data.decode().splitlines():
                    if line.startswith('CONFIG_') and '=' in line:
                        key, value = line[7:].split('=', 1); result[key] = value
                    elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
                        result[line[9:-11]] = 'n'
                return result
            base = assignments(read('board/linux.config'))
            check(base, True)
            check(base | assignments(read('board/owned-input-network.config')), True)
            shared = base | assignments(read('board/shared-clock.config'))
            check(shared, True)
            self.assertEqual(subprocess.run(['/bin/sh', str(checker), str(kernel)], capture_output=True).returncode, 0)
            check(GOOD, True)
            for vals in itertools.product(['n', 'y', 'm'], repeat=5):
                check(GOOD | dict(zip(SELECTORS, vals)), vals == ('y',)*5 or vals == ('n',)*5)
            for key in GOOD:
                if key != 'WATCHDOG_OPEN_TIMEOUT': check(GOOD | {key: 'n'}, False)
            for key in FORBIDDEN: check(GOOD | {key: 'y'}, False)
            for value in ['1', '-1', '00', '"0"', 'm']: check(GOOD | {'WATCHDOG_OPEN_TIMEOUT': value}, False)
            check(GOOD, False, 'CONFIG_GPIO_NT98513=y\n')
            check({}, False, 'CONFIG_GPIO_NT98513=y\nCONFIG_GPIO_NT98513=n\n')
            check({'MACH_NT98513': 'y'}, True)
            self.assertNotEqual(subprocess.run(['/bin/sh', str(checker), str(kernel)], capture_output=True).returncode, 0)
            check(GOOD, True)
            self.assertEqual(subprocess.run(['/bin/sh', str(checker), str(kernel)], capture_output=True).returncode, 0)
            config.unlink()
            self.assertEqual(subprocess.run(['/bin/sh', str(guard), str(config)], capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(['/bin/sh', str(checker), str(kernel)], capture_output=True).returncode, 0)

if __name__ == '__main__':
    unittest.main()
