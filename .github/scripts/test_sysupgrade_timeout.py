#!/usr/bin/env python3
"""Linux host regression: actual mount helper and flash-protection statement.

No updater top-level code runs. mount is always a finite, isolated fake; each
owned process group has an independent six-second SIGKILL deadline.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def bounded(argv, **kwargs):
    start = time.monotonic()
    proc = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True, **kwargs)
    expired = False
    try:
        output = proc.communicate(timeout=6)[0]
    except subprocess.TimeoutExpired:
        expired = True
        os.killpg(proc.pid, signal.SIGKILL)
        output = proc.communicate(timeout=1)[0]
    finally:
        # Only our newly created group; also clean up surviving descendants.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if len(output) > 65536:
        raise RuntimeError('fixture output exceeded 64 KiB')
    return dict(returncode=proc.returncode, elapsed=time.monotonic() - start,
                external_deadline=expired, output=output.decode(errors='replace'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', default='timeout', help='required executable (resolved before fixture PATH)')
    parser.add_argument('--busybox', action='store_true', help='invoke the executable with the timeout applet prefix')
    parser.add_argument('--source', type=Path, default=ROOT / 'general/overlay/usr/sbin/sysupgrade')
    args = parser.parse_args()
    executable = shutil.which(args.timeout)
    if not executable:
        parser.error('required timeout provider not found: ' + args.timeout)
    executable = os.path.abspath(executable)
    provider = [executable] + (['timeout'] if args.busybox else [])
    version = bounded([executable, '--help'] if args.busybox else provider + ['--version'])
    if version['external_deadline'] or version['returncode'] != 0:
        raise RuntimeError('cannot identify timeout provider: ' + str(version))
    print(json.dumps(dict(provider=provider, resolved_path=str(Path(executable).resolve()),
                          sha256=hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
                          version=version, path=os.environ.get('PATH'))), flush=True)
    source = args.source.read_text()
    stripped = bounded(['awk', '-f', str(ROOT / 'general/scripts/strip-shell-comments.awk'), str(args.source)])
    if stripped['returncode'] or stripped['external_deadline']:
        raise RuntimeError('production comment-stripper failed')
    failures = []
    for form, text in [('normal', source), ('stripped', stripped['output'])]:
        helper = re.findall(r'^mount_rootfs\(\) \{\n.*?^\}', text, re.M | re.S)
        # Take the production global statement, not a safe trap invented here.
        traps = re.findall(r'^trap[^\n]*', text, re.M)
        if len(helper) != 1 or len(traps) != 1:
            raise RuntimeError('expected one mount helper and one global protection statement')
        for case, stub, expected in [('success', 'exit 0', 0),
                                     ('error', 'echo "fixture mount error" >&2; exit 7', 7),
                                     ('hang', 'exec /bin/sleep 4', None)]:
            with tempfile.TemporaryDirectory(prefix='sysupgrade-timeout-') as folder:
                fixture = Path(folder)
                (fixture / 'mount').write_text('#!/bin/sh\n[ "$*" = "-t squashfs -o ro image target" ] || exit 88\n' + stub + '\n')
                (fixture / 'timeout').write_text('#!/bin/sh\nexec ' + shlex.join(provider) + ' "$@"\n')
                for name in ('mount', 'timeout'):
                    (fixture / name).chmod(0o700)
                script = helper[0] + '\n' + traps[0] + '''
mount_wait=1
cat /proc/$$/status
for sig in HUP INT TERM PIPE; do kill -s "$sig" "$$"; done
echo BEFORE_SURVIVED
mount_rootfs image target
rc=$?
cat /proc/$$/status
for sig in HUP INT TERM PIPE; do kill -s "$sig" "$$"; done
echo AFTER_SURVIVED
exit "$rc"
'''
                result = bounded(['/bin/sh', '-c', script], env=dict(os.environ, PATH=folder + ':/usr/bin:/bin'))
            masks = re.findall(r'^SigIgn:\s*([0-9a-fA-F]+)$', result['output'], re.M)
            required = sum(1 << (sig - 1) for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM, signal.SIGPIPE))
            problems = []
            if result['external_deadline']:
                problems.append('external deadline fired')
            if result['elapsed'] >= 3:
                problems.append('helper did not return below three seconds')
            if len(masks) != 2 or any(int(mask, 16) & required != required for mask in masks):
                problems.append('production parent does not ignore all protected signals before and after')
            if 'BEFORE_SURVIVED\n' not in result['output'] or 'AFTER_SURVIVED\n' not in result['output']:
                problems.append('protected parent did not survive')
            if (expected is None and result['returncode'] == 0) or (expected is not None and result['returncode'] != expected):
                problems.append('unexpected mount status')
            if case == 'error' and 'fixture mount error\n' not in result['output']:
                problems.append('ordinary error diagnostic lost')
            print(json.dumps(dict(form=form, case=case, source_sha256=hashlib.sha256(text.encode()).hexdigest(),
                                  protection=traps[0], problems=problems, **result)), flush=True)
            failures.extend(problems)
    print(json.dumps(dict(status='FAIL' if failures else 'PASS', cases=6, failures=len(failures))), flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
