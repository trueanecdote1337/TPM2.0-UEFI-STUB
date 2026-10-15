#!/usr/bin/env python3
"""Regenerate or verify the two tracked AML/HEX pairs embedded by VtpmProbeDxe."""
import argparse
from pathlib import Path
import shutil
import sys
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='Compile in a temporary directory; do not update files')
args = parser.parse_args()
for name in ('VtpmRuntimeProbe', 'VtpmWindows'):
    with tempfile.TemporaryDirectory(prefix='vtpm-iasl-') as directory:
        temporary = Path(directory)
        shutil.copyfile(root / (name + '.asl'), temporary / (name + '.asl'))
        subprocess.run(['iasl', '-tc', name + '.asl'], cwd=temporary, check=True)
        for extension in ('.aml', '.hex'):
            generated = temporary / (name + extension)
            target = root / generated.name
            if args.check:
                # HEX has compiler version/date/path comments; AML is the semantic artifact.
                if extension == '.aml' and generated.read_bytes() != target.read_bytes():
                    raise SystemExit('AML differs: ' + name + '; regenerate with the documented ACPICA version')
            else:
                shutil.copyfile(generated, target)
print('ACPI generation check PASS' if args.check else 'Updated both AML/HEX pairs')

if args.check:
    subprocess.run([sys.executable, str(root / 'scripts/verify-sources.py')], check=True)
