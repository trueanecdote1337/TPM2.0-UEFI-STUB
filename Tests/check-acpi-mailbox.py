#!/usr/bin/env python3
"""Check SystemMemory field access against ACPI table 19.34 and execute AML.
ACPICA is permissive about BufferAcc on SystemMemory, so compilation alone
is insufficient. Region simulation does not provide a real SMI response.
"""
from pathlib import Path
import re
import subprocess

base = Path(__file__).resolve().parents[1]
source = (base / 'VtpmRuntimeProbe.asl').read_text()

def validate(text):
    regions = dict(re.findall(r'OperationRegion\s*\(\s*(\w+)\s*,\s*(\w+)', text))
    allowed = {'ByteAcc', 'WordAcc', 'DWordAcc', 'QWordAcc', 'AnyAcc'}
    for name, access in re.findall(r'Field\s*\(\s*(\w+)\s*,\s*(\w+)', text):
        if regions.get(name) in {'SystemMemory', 'SystemIO'} and access not in allowed:
            raise ValueError(f'{name}: {access} is invalid for {regions[name]}')

validate(source)
old = source.replace('Field (MBXR, ByteAcc, NoLock, Preserve)',
                     'Field (MBXR, BufferAcc, NoLock, Preserve)')
try:
    validate(old)
except ValueError:
    pass
else:
    raise AssertionError('Regression: the invalid BufferAcc must be rejected')

result = subprocess.run(['acpiexec', '-b',
    r'execute \_SB.VPRB._INI; execute \_SB.VPRB.VTCM; evaluate \_SB.VPRB.TREQ',
    str(base / 'VtpmRuntimeProbe.aml')], capture_output=True, text=True, timeout=15)
output = result.stdout + result.stderr
(base / 'Tests/acpi-mailbox.log').write_text(output)
assert result.returncode == 0, output
assert 'ACPI Error' not in output and 'ACPI Exception' not in output, output
assert '[Buffer] Length 40' in output, output  # 0x40 bytes, despite ByteAcc
assert '80 01 00 00 00 16 00 00 01 7A' in output, output
print('ACPI region/access validation PASS; invalid BufferAcc regression detected')
print('AML _INI/request-buffer execution PASS; Windows/real SMI retest still required')
