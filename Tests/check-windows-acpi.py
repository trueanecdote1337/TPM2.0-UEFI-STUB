#!/usr/bin/env python3
"""Compile/execute the Windows-facing AML with ACPICA's simulated regions."""
from pathlib import Path
import subprocess
import tempfile
b=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vtpm-aml-test-') as directory:
    probe=Path(directory)/'WindowsAcpiProbe'
    subprocess.run(['iasl','-p',str(probe),str(b/'Tests/WindowsAcpiProbe.asl')],
                   check=True,capture_output=True,text=True)
    result=subprocess.run(['acpiexec','-b',
        r'evaluate \_SB.VTPM._HID; evaluate \_SB.VTPM._CRS; execute WQRY; execute WSTR; execute WREV',
        str(b/'VtpmRuntimeProbe.aml'), str(b/'VtpmWindows.aml'), str(probe.with_suffix('.aml'))],
        capture_output=True,text=True,timeout=15)
s=result.stdout+result.stderr
(b/'Tests/windows-acpi.log').write_text(s)
assert result.returncode==0 and 'ACPI Error' not in s and 'ACPI Exception' not in s and 'ACPI Warning' not in s,s
assert '"MSFT0101"' in s and '[Buffer] Length 0E' in s,s
# Resource bytes: exactly one Memory32Fixed descriptor (12 bytes) + EndTag.
assert '00 30 00 00 79 00' in s,s
assert '47 01 B2 00' not in s,s
assert '[Buffer] Length 01 =     0000: 03' in s,s
# Simulated I/O does not produce the A5 SMM ACK: Start must report failure.
assert '[Integer] = 0000000000000001' in s,s
assert '[Buffer] Length 01 =     0000: 00' in s,s
print('Windows AML HID/resources/DSM revision/support/failed-start checks PASS')
