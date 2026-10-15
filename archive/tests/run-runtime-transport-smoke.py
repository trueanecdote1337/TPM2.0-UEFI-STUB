#!/usr/bin/env python3
"""Boot fresh OVMF variables without guest disks; verify the shared-buffer transport uses the single SMM TPM core."""
from pathlib import Path
import shutil
import subprocess
import tempfile

base = Path(__file__).resolve().parents[2]
fv = base / 'Build/OvmfX64/DEBUG_GCC5/FV'
for name, cpu, expected in [('runtime-transport-smoke', 'max', 'PASS'),
                            ('runtime-no-core', 'max,-rdrand', 'FAIL')]:
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
        for attempt in range(40):
            try:
                process.communicate(timeout=1)
                break
            except subprocess.TimeoutExpired:
                if log.exists() and ('PASS: TPM GetRandom through shared buffer' if expected == 'PASS' else 'FAIL: TPM shared-buffer test=5 result=4') in log.read_text(errors='replace'):
                    break
        if process.poll() is None:
            process.terminate()
            process.communicate(timeout=5)
    text = log.read_text(errors='replace')
    print(name)
    for line in text.splitlines():
        if '[VTPM-CMD]' in line or 'TPM command=' in line or 'TPM shared-buffer' in line or 'through shared buffer' in line or 'ASSERT' in line:
            print(line)
    assert f'[VTPM-RNG] {expected}: RDRAND source' in text, log
    assert '[VTPM-CLOCK] PASS: calibrated TSC; mask=0x1F' in text, log
    assert '[VTPM-PLAT] PASS: platform callbacks; mask=0x1F' in text, log
    if expected == 'PASS':
        assert '[VTPM-CMD] PASS: ExecuteCommand; mask=0x3F stage=0' in text, log
        assert 'SMM core protocol installation: Success' in text, log
        assert 'PASS: TPM GetCapability through shared buffer' in text, log
        assert 'PASS: TPM GetRandom through shared buffer' in text, log
    else:
        assert '[VTPM-CMD] FAIL: ExecuteCommand; mask=0x0 stage=3' in text, log
        assert 'FAIL: TPM shared-buffer test=5 result=4' in text, log
    assert '[VTPM-CORE] PASS:' in text and '[VTPM-NV] PASS:' in text, log
    assert 'PASS: shared buffer and bounds checks verified' in text, log
    assert 'ASSERT' not in text, log
print('Transport tests passed; after-EBS ACPI execution requires a guest OS')
