#!/usr/bin/env python3
"""Refuse changing another variant's output before invoking Buildroot."""
import argparse
from pathlib import Path
import sys


def check(output):
    path = Path(output)
    if not output or not path.is_absolute() or ".." in path.parts:
        raise ValueError("output must be a nonempty absolute path without parent traversal")
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError('symlink output path refused: ' + str(part))
    if not path.exists():
        return
    if not path.is_dir():
        raise ValueError('output is not a directory')
    config = path / '.config'
    if config.is_symlink():
        raise ValueError('symlink configuration refused')
    if not config.exists():
        if any(path.iterdir()):
            raise ValueError('output must be empty or already Ethernet configured')
        return
    if not config.is_file():
        raise ValueError('configuration is not a regular file')
    external = Path(__file__).resolve().parents[1]
    expected = {
        'BR2_LINUX_KERNEL': 'y',
        'BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE': '"' + str(external / 'board/linux.config') + '"',
        'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES': '"' + str(external / 'board/shared-clock.config') + ' ' + str(external / 'board/ethernet.config') + '"',
    }
    values = {}
    for line in config.read_text().splitlines():
        if '=' not in line or not line.startswith('BR2_'):
            continue
        key, value = line.split('=', 1)
        if key in values:
            raise ValueError('duplicate configuration key: ' + key)
        values[key] = value
    # Buildroot retains external-variable references in its saved configuration.
    # Expand only this one documented token, never evaluate configuration text.
    normalized = {key: value.replace('$(BR2_EXTERNAL_NT98513_PATH)', str(external))
                  for key, value in values.items()}
    if any(normalized.get(key) != value for key, value in expected.items()):
        raise ValueError('refusing a different variant or unrelated output')
    if values.get('BR2_PACKAGE_DIVINUS_OWNED_NT98513', 'n') != 'n':
        raise ValueError('Ethernet output must not contain Divinus selection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    args = parser.parse_args()
    try:
        check(args.output)
    except (OSError, UnicodeError, ValueError) as exc:
        print('NT98513: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
