#!/usr/bin/env python3
"""Boot the vTPM OVMF firmware, optionally attaching an existing Windows disk."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def drive_path(path):
    # QEMU -drive uses comma-separated options even when argv is correctly quoted.
    return str(path).replace(',', ',,')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fv-dir', type=Path, default=os.environ.get('VTPM_FV_DIR'))
    parser.add_argument('--disk', type=Path, help='Existing guest disk; QEMU can write to it')
    parser.add_argument('--disk-format', choices=('raw', 'qcow2'), default='raw')
    parser.add_argument('--vars-file', type=Path, help='Persistent UEFI variables; initialized only if absent')
    parser.add_argument('--accel', choices=('tcg', 'kvm'), default='tcg')
    parser.add_argument('--cpus', type=int, default=2)
    parser.add_argument('--memory', type=int, default=4096, help='Guest RAM in MiB')
    parser.add_argument('--log-dir', type=Path, default=ROOT / 'artifacts/qemu')
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--dry-run', action='store_true', help='Print argv; do not launch or create files')
    args = parser.parse_args()
    if args.cpus < 1 or args.memory < 512:
        parser.error('Use at least one vCPU and 512 MiB RAM')
    if args.fv_dir is None:
        edk2 = os.environ.get('EDK2_DIR')
        if edk2 is None:
            parser.error('Pass --fv-dir, VTPM_FV_DIR, or EDK2_DIR')
        args.fv_dir = Path(edk2) / 'Build/OvmfX64/DEBUG_GCC5/FV'
    fv = args.fv_dir.resolve()
    code, template = fv / 'OVMF_CODE.fd', fv / 'OVMF_VARS.fd'
    for path in (code, template):
        if not path.is_file():
            parser.error('Missing firmware: ' + str(path))
    if args.disk is not None:
        args.disk = args.disk.resolve()
        if not args.disk.is_file():
            parser.error('Missing guest disk: ' + str(args.disk))
        if args.disk in (code, template):
            parser.error('A firmware file cannot be used as a guest disk')
    if args.vars_file is not None:
        args.vars_file = args.vars_file.resolve()
        if args.vars_file in (code, template, args.disk):
            parser.error('--vars-file must be separate from the firmware template and guest disk')
        if args.vars_file.exists() and args.vars_file.stat().st_size != template.stat().st_size:
            parser.error('--vars-file size does not match this firmware build')
    if shutil.which('qemu-system-x86_64') is None:
        parser.error('qemu-system-x86_64 is required')
    # Temporary VARS is the default. Explicit --vars-file is never reset.
    with tempfile.TemporaryDirectory(prefix='vtpm-guest-') if not args.dry_run else _NoTemporary() as temporary:
        variables = args.vars_file or (Path(temporary) / 'VARS.fd' if temporary else Path('<temporary-VARS>'))
        timestamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid())
        log = args.log_dir.resolve() / ('edk2-' + timestamp + '.log')
        command = ['qemu-system-x86_64', '-machine', 'q35,smm=on', '-accel', args.accel,
                   '-cpu', 'host' if args.accel == 'kvm' else 'max',
                   '-m', str(args.memory), '-smp', str(args.cpus),
                   '-global', 'ICH9-LPC.disable_s3=1',
                   '-global', 'ICH9-LPC.x-smi-broadcast=on',
                   '-global', 'ICH9-LPC.x-smi-swsmi-timer=on',
                   '-drive', 'if=pflash,format=raw,readonly=on,file=' + drive_path(code),
                   '-drive', 'if=pflash,format=raw,file=' + drive_path(variables),
                   '-net', 'none', '-debugcon', 'file:' + str(log),
                   '-global', 'isa-debugcon.iobase=0x402', '-serial', 'stdio']
        if args.disk:
            command += ['-drive', 'if=none,id=disk0,format=' + args.disk_format + ',file=' + drive_path(args.disk),
                        '-device', 'ide-hd,drive=disk0,bus=ide.0', '-boot', 'order=c']
        if args.headless:
            command += ['-display', 'none']
        if args.dry_run:
            print(json.dumps(command, indent=2))
            return 0
        args.log_dir.mkdir(parents=True, exist_ok=True)
        if not variables.exists():
            variables.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(template, variables)
        print('Firmware log: ' + str(log), flush=True)
        process = subprocess.Popen(command)
        try:
            return process.wait()
        except KeyboardInterrupt:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            return 130


class _NoTemporary:
    def __enter__(self):
        return None

    def __exit__(self, *args):
        return False


if __name__ == '__main__':
    sys.exit(main())
