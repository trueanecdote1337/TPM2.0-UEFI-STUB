#!/usr/bin/env python3
"""Missing deferred timer must not expose a dead MSFT0101 TPM to the guest."""
from pathlib import Path
import tempfile,shutil,subprocess
b=Path(__file__).resolve().parents[2]
fv=b/'Build/OvmfX64/DEBUG_GCC5/FV'
log=Path(__file__).with_name('windows-no-timer.log')
with tempfile.TemporaryDirectory(prefix='vtpm-no-timer-') as d:
    var=Path(d)/'VARS.fd'; shutil.copyfile(fv/'OVMF_VARS.fd',var)
    p=subprocess.Popen(['qemu-system-x86_64','-machine','q35,smm=on','-accel','tcg',
        '-cpu','max','-m','2048','-smp','2','-global','ICH9-LPC.disable_s3=1',
        '-global','ICH9-LPC.x-smi-broadcast=on','-global','ICH9-LPC.x-smi-swsmi-timer=off','-display','none','-serial','none','-net','none',
        '-drive',f'if=pflash,format=raw,readonly=on,file={fv}/OVMF_CODE.fd',
        '-drive',f'if=pflash,format=raw,file={var}', '-debugcon',f'file:{log}',
        '-global','isa-debugcon.iobase=0x402'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for i in range(40):
        try: p.communicate(timeout=1); break
        except subprocess.TimeoutExpired:
            if log.exists() and '[VTPM-WIN] FAIL: deferred worker unavailable or slow;' in log.read_text(errors='replace'): break
    if p.poll() is None: p.terminate(); p.communicate(timeout=5)
s=log.read_text(errors='replace')
assert '[VTPM-WIN] FAIL: deferred worker unavailable or slow;' in s,s[-3000:]
assert '[VTPM-WIN] PASS: TPM2/MSFT0101 published;' not in s
assert '[VTPM-CTRL] PASS: snapshot, deferred execution and recovery verified' in s
assert 'ASSERT' not in s
print('Missing-timer negative test PASS: TPM2/MSFT0101 not published')
