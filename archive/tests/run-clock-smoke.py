#!/usr/bin/env python3
"""Boot fresh OVMF variables without guest disks; verify the TPM clock probe."""
from pathlib import Path
import shutil
import subprocess
import tempfile

base = Path(__file__).resolve().parents[2]
fv = base / 'Build/OvmfX64/DEBUG_GCC5/FV'
for name, cpu, expected in [('clock-smoke', 'max', 'PASS')]:
    log = Path(__file__).parent / (name + '.log')
    with tempfile.TemporaryDirectory(prefix='vtpm-rng-') as tmp:
        variables = Path(tmp) / 'VARS.fd'
        shutil.copyfile(fv / 'OVMF_VARS.fd', variables)
        args = ['qemu-system-x86_64', '-machine', 'q35,smm=on', '-accel', 'tcg',
                '-cpu', cpu, '-m', '2048', '-smp', '2', '-global',
                'ICH9-LPC.disable_s3=1', '-display', 'none', '-serial', 'none',
                '-net', 'none', '-drive',
                f'if=pflash,format=raw,readonly=on,file={fv}/OVMF_CODE.fd',
                '-drive', f'if=pflash,format=raw,file={variables}',
                '-debugcon', f'file:{log}', '-global', 'isa-debugcon.iobase=0x402']
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.communicate(timeout=5)
    text = log.read_text(errors='replace')
    print(name)
    for line in text.splitlines():
        if '[VTPM-CLOCK]' in line or 'ASSERT' in line:
            print(line)
    assert '[VTPM-RNG] PASS: RDRAND source' in text, log
    assert '[VTPM-CLOCK] PASS: calibrated TSC; mask=0x1F' in text, log
    assert '[VTPM-CORE] PASS:' in text and '[VTPM-NV] PASS:' in text, log
    assert 'PASS: shared buffer and bounds checks verified' in text, log
    assert 'ASSERT' not in text, log
print('Clock and previous probes PASS')
