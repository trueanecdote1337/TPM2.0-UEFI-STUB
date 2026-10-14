#!/usr/bin/env python3
"""Measure the QEMU timer / OVMF MP rendezvous mismatch with two vCPUs."""
from pathlib import Path
import tempfile,shutil,subprocess,re
b=Path(__file__).resolve().parents[2]; fv=b/'Build/OvmfX64/DEBUG_GCC5/FV'
for name,broadcast in [('smi-latency-broadcast','on'),('smi-latency-relaxed-rejected','off')]:
    log=Path(__file__).with_name(name+'.log')
    with tempfile.TemporaryDirectory(prefix='vtpm-smi-latency-') as d:
        var=Path(d)/'VARS.fd'; shutil.copyfile(fv/'OVMF_VARS.fd',var)
        p=subprocess.Popen(['qemu-system-x86_64','-machine','q35,smm=on','-accel','kvm',
            '-cpu','host','-m','2048','-smp','2','-global','ICH9-LPC.disable_s3=1',
            '-global',f'ICH9-LPC.x-smi-broadcast={broadcast}', '-display','none','-serial','none','-net','none',
            '-drive',f'if=pflash,format=raw,readonly=on,file={fv}/OVMF_CODE.fd',
            '-drive',f'if=pflash,format=raw,file={var}', '-debugcon',f'file:{log}',
            '-global','isa-debugcon.iobase=0x402'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        marker='[VTPM-WIN] PASS: TPM2/MSFT0101 published;' if broadcast=='on' else '[VTPM-WIN] FAIL: deferred worker unavailable or slow;'
        for i in range(40):
            try: p.communicate(timeout=1); break
            except subprocess.TimeoutExpired:
                if log.exists() and marker in log.read_text(errors='replace'): break
        if p.poll() is None: p.terminate(); p.communicate(timeout=5)
    s=log.read_text(errors='replace')
    assert marker in s,s[-3000:]
    assert 'ASSERT' not in s
    if broadcast=='on':
        m=re.search(r'deferred round-trip=(\d+) us',s); assert m,s[-3000:]
        elapsed=int(m.group(1)); print(name,elapsed,'us')
        assert elapsed<=250000
        assert '[VTPM-SMI] PASS: all 2 CPUs in SMM before timer worker' in s
    else:
        assert '[VTPM-SMI] Refusing relaxed CPU sync;' in s
        assert 'TPM2/MSFT0101 published;' not in s
        print(name,'unsafe relaxed delivery blocked')
print('SMI rendezvous regression PASS: traditional delivery is fast; relaxed setup is rejected')
