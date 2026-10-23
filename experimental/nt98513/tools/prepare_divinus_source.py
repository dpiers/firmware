#!/usr/bin/env python3
"""Prepare the reviewed source-only archive offline from immutable Git blobs."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tarfile
import tempfile

TREE = Path(__file__).resolve().parents[1]
PACKAGE = TREE / 'package/divinus-owned-nt98513'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_manifest():
    return json.loads((PACKAGE / 'source-manifest.json').read_text())


def validate_manifest(m):
    if not re.fullmatch(r'[0-9a-f]{40}', m['commit']) or not re.fullmatch(r'[0-9a-f]{64}', m['version']):
        raise ValueError('invalid source identity')
    if m['archive'] != 'divinus-owned-nt98513-' + m['version'] + '.tar.gz':
        raise ValueError('invalid archive name')
    if len(m['files']) != 48 or len(m['closure']) != 39 or len(set(m['closure'])) != 39:
        raise ValueError('invalid source member count')
    if not set(m['closure']) <= m['files'].keys() or 'PACKAGE.md' not in m['files']:
        raise ValueError('invalid source closure')
    for name, value in m['files'].items():
        path = PurePosixPath(name)
        if path.is_absolute() or str(path) != name or '..' in path.parts or not re.fullmatch(r'[A-Za-z0-9_.+/-]+', name):
            raise ValueError('unsafe source member: ' + repr(name))
        if not re.fullmatch(r'[0-9a-f]{64}', value):
            raise ValueError('invalid source digest')
    if not re.fullmatch(r'[0-9a-f]{64}', m['sha256']):
        raise ValueError('invalid archive digest')


def read_blob(checkout, commit, name):
    env = os.environ.copy()
    env['GIT_NO_REPLACE_OBJECTS'] = '1'
    result = subprocess.run(['git', '-C', str(checkout), 'show', commit + ':' + name],
                            capture_output=True, env=env, check=False)
    if result.returncode:
        raise ValueError('missing pinned Git blob: ' + name)
    return result.stdout


def safe_path(path):
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('use an absolute path without parent traversal')
    for part in reversed((path, *path.parents)):
        if part.is_symlink():
            raise ValueError('symlink path refused: ' + str(part))
        if part.exists() and part != path and not part.is_dir():
            raise ValueError('non-directory path component')
    return path


def check_archive(path, m):
    safe_path(path)
    if not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('prepared archive is missing or not regular')
    if digest(path.read_bytes()) != m['sha256']:
        raise ValueError('prepared archive hash mismatch; existing data preserved')


def check_build_output(build_output, variant):
    """Identify the selected consumer variant before Buildroot can rewrite it."""
    fragments = {
        'mmc': 'shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config $(BR2_EXTERNAL_NT98513_PATH)/board/usb-host.config $(BR2_EXTERNAL_NT98513_PATH)/board/wifi.config $(BR2_EXTERNAL_NT98513_PATH)/board/mmc.config',
        'wifi': 'shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config $(BR2_EXTERNAL_NT98513_PATH)/board/usb-host.config $(BR2_EXTERNAL_NT98513_PATH)/board/wifi.config',
        'usb-host': 'shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config $(BR2_EXTERNAL_NT98513_PATH)/board/usb-host.config',
        'owned-input': 'owned-input-network.config',
        'ethernet-owned-input': 'shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config',
    }
    if variant not in fragments:
        raise ValueError('unknown build variant')
    out = safe_path(build_output)
    if not out.exists():
        return
    if not out.is_dir():
        raise ValueError('build output is not a directory')
    config = safe_path(out / '.config')
    if not config.exists():
        if any(out.iterdir()):
            raise ValueError('build output must be empty or already ' + variant + ' configured')
        return
    if not config.is_file():
        raise ValueError('build configuration is not a regular file')
    values = {}
    for line in config.read_text().splitlines():
        disabled = re.fullmatch(r'# (BR2_[A-Za-z0-9_]+) is not set', line)
        if disabled:
            key, value = disabled[1], 'n'
        elif not line or line.startswith('#'):
            continue
        else:
            entry = re.fullmatch(r'(BR2_[A-Za-z0-9_]+)=("(?:[^"\\]|\\.)*"|y|n|m|[0-9]+|0[xX][0-9a-fA-F]+)', line)
            if not entry:
                raise ValueError('malformed build configuration entry')
            key, value = entry.groups()
        if key in values:
            raise ValueError('duplicate build configuration key: ' + key)
        values[key] = value.replace('$(BR2_EXTERNAL_NT98513_PATH)', str(TREE))
    expected = {
        'BR2_LINUX_KERNEL': 'y',
        'BR2_STATIC_LIBS': 'y',
        'BR2_PACKAGE_DIVINUS_OWNED_NT98513': 'y',
        'BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE': '"' + str(TREE / 'board/linux.config') + '"',
        'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES': '"$(BR2_EXTERNAL_NT98513_PATH)/board/' + fragments[variant] + '"',
        'BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES': '"' + str(TREE / 'board/owned-input-network-busybox.config') + '"',
    }
    if variant in ('wifi', 'mmc'):
        expected['BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES'] = '"' + str(TREE / 'board/owned-input-network-busybox.config') + ' ' + str(TREE / 'board/wifi-busybox.config') + '"'
        for key in ('BR2_PACKAGE_WPA_SUPPLICANT_NT98513', 'BR2_PACKAGE_RTL8188FU_FIRMWARE_NT98513',
                    'BR2_PACKAGE_NT98513_WIFI_CONTROL', 'BR2_PACKAGE_WIRELESS_REGDB'):
            expected[key] = 'y'
        if values.get('BR2_PACKAGE_WPA_SUPPLICANT', 'n') != 'n':
            raise ValueError('shared supplicant conflicts with isolated Wi-Fi package')
    expected = {key: value.replace('$(BR2_EXTERNAL_NT98513_PATH)', str(TREE))
                for key, value in expected.items()}
    if any(values.get(key) != value for key, value in expected.items()):
        raise ValueError('refusing a different variant or unrelated output; expected ' + variant)


def verify(output_dir, build_output=None, build_variant='owned-input'):
    m = load_manifest(); validate_manifest(m)
    directory = safe_path(output_dir)
    archive = directory / m['archive']
    check_archive(archive, m)
    if build_output is not None:
        check_build_output(build_output, build_variant)
    return archive


def prepare(checkout, output_dir):
    m = load_manifest(); validate_manifest(m)
    directory = safe_path(output_dir)
    checkout = Path(checkout).resolve(strict=True)
    for protected in (checkout, TREE.parents[1]):
        if directory == protected or protected in directory.parents:
            raise ValueError('source cache must be outside source checkouts')
    archive = directory / m['archive']
    safe_path(archive)
    if directory.exists() and not directory.is_dir():
        raise ValueError('source cache is not a directory')
    if archive.exists():
        check_archive(archive, m)
    blobs = {}
    for name, expected in sorted(m['files'].items()):
        data = (PACKAGE / name).read_bytes() if name == 'PACKAGE.md' else read_blob(checkout, m['commit'], name)
        if digest(data) != expected:
            raise ValueError('source member hash mismatch: ' + name)
        blobs[name] = data
    # Keep all validation before creating the output directory or publishing data.
    buf = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=buf, mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as tf:
            for name, data in blobs.items():
                info = tarfile.TarInfo(m['archive'].removesuffix('.tar.gz') + '/' + name)
                info.size = len(data); info.mode = 0o644; info.mtime = info.uid = info.gid = 0
                tf.addfile(info, io.BytesIO(data))
    data = buf.getvalue()
    if digest(data) != m['sha256']:
        raise ValueError('archive reproduction differs; refusing new identity')
    if archive.exists():
        return archive
    directory.mkdir(parents=True, exist_ok=True)
    safe_path(directory)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=directory, prefix='.divinus-source-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.chmod(temporary, 0o644)
        # Hard-link publication refuses replacement, including concurrent symlinks.
        try:
            os.link(temporary, archive)
        except FileExistsError:
            check_archive(archive, m)
    finally:
        if temporary is not None:
            temporary.unlink()
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkout', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--reject-wifi-output', type=Path)
    parser.add_argument('--verify-archive', action='store_true')
    parser.add_argument('--build-output', type=Path)
    parser.add_argument('--build-variant', choices=('owned-input', 'ethernet-owned-input', 'usb-host', 'wifi', 'mmc'), default='owned-input')
    args = parser.parse_args()
    try:
        if args.reject_wifi_output is not None:
            if args.output_dir or args.checkout or args.verify_archive or args.build_output:
                raise ValueError('--reject-wifi-output is a standalone guard')
            config = safe_path(args.reject_wifi_output) / '.config'
            safe_path(config)
            if config.exists() and ('/board/wifi.config' in config.read_text() or
                                    re.search(r'^BR2_PACKAGE_(?:NT98513_WIFI_CONTROL|WPA_SUPPLICANT_NT98513|RTL8188FU_FIRMWARE_NT98513)=y$', config.read_text(), re.M)):
                raise ValueError('refusing Wi-Fi output reuse by a legacy RAM target')
            return
        if args.output_dir is None:
            raise ValueError('--output-dir is required')
        if args.verify_archive:
            if args.checkout:
                raise ValueError('--checkout is not used by verification')
            archive = verify(args.output_dir, args.build_output, args.build_variant)
        else:
            if not args.checkout or args.build_output or args.build_variant != 'owned-input':
                raise ValueError('preparation needs --checkout; --build-output is verification-only')
            archive = prepare(args.checkout, args.output_dir)
        print(json.dumps({'archive': str(archive), 'sha256': digest(archive.read_bytes())}))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, 'source preparation: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
