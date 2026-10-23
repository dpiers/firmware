#!/usr/bin/env python3
"""Identify MMC outputs before Make can configure or package another variant."""
import argparse
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True
TREE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mmc_source_guard', TREE/'tools/prepare_divinus_source.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


def reject_mmc(output):
    # The legacy packager already resolves aliases (e.g. macOS /var). Keep
    # its existing path semantics; strict admission is only for MMC outputs.
    path = Path(output).resolve()
    if not path.exists():
        return
    if not path.is_dir():
        raise ValueError('output is not a directory')
    config = guard.safe_path(path/'.config')
    if config.exists():
        if not config.is_file():
            raise ValueError('configuration is not a regular file')
        if '/board/mmc.config' in config.read_text():
            raise ValueError('refusing MMC output for a legacy RAM target')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reject-mmc', action='store_true')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        if args.reject_mmc:
            reject_mmc(args.output)
        else:
            guard.check_build_output(args.output, 'mmc')
    except (OSError, UnicodeError, ValueError) as exc:
        print('NT98513 MMC: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
