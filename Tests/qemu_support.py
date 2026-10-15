"""Shared, disk-free QEMU runner for firmware regression checks (Python 3.8+)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def parser(description):
    result = argparse.ArgumentParser(description=description)
    result.add_argument('--fv-dir', type=Path, default=os.environ.get('VTPM_FV_DIR'))
    result.add_argument('--log-dir', type=Path, default=ROOT / 'artifacts/tests')
    result.add_argument('--accel', choices=('tcg', 'kvm'), default=os.environ.get('VTPM_SMOKE_ACCEL', 'tcg'))
    result.add_argument('--cpus', type=int, default=int(os.environ.get('VTPM_SMOKE_CPUS', '2')))
    result.add_argument('--timeout', type=float, default=60)
    return result


def configure(args):
    if args.cpus < 1 or args.timeout <= 0:
        raise ValueError('--cpus and --timeout must be positive')
    if args.fv_dir is None:
        edk2 = os.environ.get('EDK2_DIR')
        if edk2 is None and (ROOT.parent / 'edksetup.sh').exists():
            edk2 = str(ROOT.parent)
        if edk2 is None:
            raise ValueError('Pass --fv-dir, VTPM_FV_DIR, or EDK2_DIR; no firmware path can be inferred')
        args.fv_dir = Path(edk2) / 'Build/OvmfX64/DEBUG_GCC5/FV'
    args.fv_dir = args.fv_dir.resolve()
    for filename in ('OVMF_CODE.fd', 'OVMF_VARS.fd'):
        if not (args.fv_dir / filename).is_file():
            raise ValueError('Missing firmware: ' + str(args.fv_dir / filename))
    if shutil.which('qemu-system-x86_64') is None:
        raise ValueError('qemu-system-x86_64 is required')
    args.log_dir.mkdir(parents=True, exist_ok=True)
    return args


def boot(args, name, marker, rdrand=True, broadcast=True, timer=True):
    """Never attach guest disks; kill QEMU and clean temporary VARS on all exits."""
    cpu = 'host' if args.accel == 'kvm' else 'max'
    if not rdrand:
        cpu += ',-rdrand'
    log = args.log_dir / (name + '.log')
    with tempfile.TemporaryDirectory(prefix='vtpm-smoke-') as directory:
        variables = Path(directory) / 'VARS.fd'
        shutil.copyfile(args.fv_dir / 'OVMF_VARS.fd', variables)
        command = ['qemu-system-x86_64', '-machine', 'q35,smm=on', '-accel', args.accel,
                   '-cpu', cpu, '-m', '2048', '-smp', str(args.cpus),
                   '-global', 'ICH9-LPC.disable_s3=1',
                   '-global', 'ICH9-LPC.x-smi-broadcast=' + ('on' if broadcast else 'off'),
                   '-global', 'ICH9-LPC.x-smi-swsmi-timer=' + ('on' if timer else 'off'),
                   '-display', 'none', '-serial', 'none', '-net', 'none',
                   '-drive', 'if=pflash,format=raw,readonly=on,file=' + str(args.fv_dir / 'OVMF_CODE.fd'),
                   '-drive', 'if=pflash,format=raw,file=' + str(variables),
                   '-debugcon', 'file:' + str(log), '-global', 'isa-debugcon.iobase=0x402']
        # Clear a previous result before launch; stale PASS markers are invalid.
        log.write_text('')
        with tempfile.TemporaryFile(mode='w+') as errors:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=errors)
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    text = log.read_text(errors='replace')
                    if marker in text or process.poll() is not None:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                errors.seek(0)
                error_text = errors.read()
        text = log.read_text(errors='replace')
    for forbidden in ('ASSERT', 'Access SMM communication forbidden address', '!!!! X64 Exception'):
        if forbidden in text:
            raise RuntimeError('Firmware fault (' + forbidden + '): ' + str(log))
    if marker not in text:
        raise RuntimeError('Expected marker not found: ' + marker + '\nLog: ' + str(log) + '\n' + error_text[-2000:])
    print(name + ': PASS (' + str(log) + ')')
    return text


def require(text, *markers):
    for marker in markers:
        if marker not in text:
            raise RuntimeError('Missing check: ' + marker)
