#!/usr/bin/env python3
"""Verify the vendored TPM and EDK/OpenSSL headers against their recorded hashes."""
import hashlib
import json
import re
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1] / 'TpmCoreLib'
failures = []
count = 0
for manifest, prefix in [('UPSTREAM_SHA256.json', root / 'Vendor'), ('EDK_HEADERS_SHA256.json', root)]:
    for name, entry in json.loads((root / manifest).read_text()).items():
        expected = entry['sha256'] if isinstance(entry, dict) else entry
        path = prefix / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            failures.append(str(path.relative_to(root)))
        count += 1
if failures:
    sys.exit('Source verification FAILED:\n' + '\n'.join(failures))
print('Source verification PASS: ' + str(count) + ' files match the recorded snapshots')

project = root.parent
for name in ('VtpmRuntimeProbe', 'VtpmWindows'):
    source = (project / (name + '.hex')).read_text()
    array = source.split('[] =', 1)[1].split('};', 1)[0]
    array = re.sub(r'/\*.*?\*/', '', array, flags=re.S)
    data = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{2})\b', array))
    if data != (project / (name + '.aml')).read_bytes():
        sys.exit('Embedded HEX does not match AML: ' + name)
print('Embedded ACPI arrays PASS: both HEX arrays match their AML files')
