#!/usr/bin/env python3
"""Integrate this package into the pinned OVMF checkout without replacing files."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EDK2_REVISION = '49fa59e82e4c6ea798f65fc4e5948eae63ad6e07'
CPU_ORIGINAL = 'SmmCpuFeaturesLib|OvmfPkg/Library/SmmCpuFeaturesLib/SmmCpuFeaturesLib.inf'
CPU_VTPM = 'SmmCpuFeaturesLib|MyTpm2EMU/VtpmSmmCpuFeaturesLib/VtpmSmmCpuFeaturesLib.inf'
MAPPING = '  TpmCoreLib|MyTpm2EMU/TpmCoreLib/TpmCoreLib.inf\n'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Expected exactly one integration anchor: ' + old.strip())
    return text.replace(old, new, 1)


def transform(text, kind, remove=False):
    """Idempotent forward/reverse edits; unrelated settings stay intact."""
    include = '!include MyTpm2EMU/integration/Components.' + kind + '.inc\n'
    anchor = ('  ' if kind == 'dsc' else 'INF  ') + 'OvmfPkg/SmmControl2Dxe/SmmControl2Dxe.inf\n'
    if remove:
        if include not in text:
            if 'MyTpm2EMU/' in text:
                raise ValueError('Unrecognized pre-existing vTPM integration; inspect manually')
            return text
        text = replace_once(text, include, '')
        if kind == 'dsc':
            text = replace_once(text, MAPPING, '')
            text = replace_once(text, CPU_VTPM, CPU_ORIGINAL)
        return text
    if include in text:
        if kind == 'dsc' and (text.count(MAPPING) != 1 or text.count(CPU_VTPM) != 1):
            raise ValueError('Incomplete vTPM integration; inspect manually')
        return text
    if 'MyTpm2EMU/' in text:
        raise ValueError('Pre-existing vTPM edits found; use a clean checkout')
    text = replace_once(text, anchor, include + anchor)
    if kind == 'dsc':
        crypto = '  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/SmmCryptLib.inf\n'
        text = replace_once(text, crypto, crypto + MAPPING)
        text = replace_once(text, CPU_ORIGINAL, CPU_VTPM)
    return text


def atomic_write(path, data):
    mode = path.stat().st_mode
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edk2', required=True, type=Path)
    parser.add_argument('--dry-run', action='store_true', help='Print a diff without changing files')
    parser.add_argument('--remove', action='store_true', help='Undo only this script\'s integration edits')
    args = parser.parse_args()
    edk2 = args.edk2.resolve()
    if not (edk2 / 'edksetup.sh').is_file():
        parser.error('Not an EDK II checkout: ' + str(edk2))
    revision = subprocess.run(['git', '-C', str(edk2), 'rev-parse', 'HEAD'],
                              capture_output=True, text=True, check=True).stdout.strip()
    if revision != EDK2_REVISION and not args.remove:
        parser.error('Supported EDK II revision is ' + EDK2_REVISION + '; found ' + revision)
    target = edk2 / 'MyTpm2EMU'
    if target.exists() or target.is_symlink():
        if not target.exists() or target.resolve() != ROOT:
            parser.error('MyTpm2EMU already exists and does not point to this repository; refusing overwrite')
    prepared = []
    for kind in ('dsc', 'fdf'):
        path = edk2 / ('OvmfPkg/OvmfPkgX64.' + kind)
        before = path.read_bytes()
        newline = b'\r\n' if b'\r\n' in before else b'\n'
        text = before.decode().replace('\r\n', '\n')
        changed = transform(text, kind, args.remove)
        after = changed.replace('\n', newline.decode()).encode()
        prepared.append((path, before, after))
        if args.dry_run:
            print(''.join(difflib.unified_diff(text.splitlines(True), changed.splitlines(True),
                                             fromfile=str(path), tofile=str(path))), end='')
    if args.dry_run:
        print('Dry run complete; no files changed.')
        return
    linked = False
    written = []
    try:
        if not args.remove and not target.exists():
            target.symlink_to(ROOT, target_is_directory=True)
            linked = True
        for path, before, after in prepared:
            if before != after:
                atomic_write(path, after)
                written.append((path, before))
    except BaseException:
        for path, before in reversed(written):
            atomic_write(path, before)
        if linked:
            target.unlink()
        raise
    # An in-tree clone is never removed. Only unlink a symlink to this repo.
    if args.remove and target.is_symlink() and target.resolve() == ROOT:
        target.unlink()
    print(('Removed' if args.remove else 'Installed') + ' vTPM OVMF integration: ' + str(edk2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
