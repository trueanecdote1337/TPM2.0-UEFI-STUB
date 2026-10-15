#!/usr/bin/env python3
"""Check core, transports, all-CPU SMM rendezvous, and ACPI publication; no Windows disk."""
from qemu_support import boot, configure, parser, require


def main():
    args = configure(parser(__doc__).parse_args())
    text = boot(args, 'transport-' + args.accel + '-' + str(args.cpus) + 'cpu',
                '[VTPM-WIN] PASS: TPM2/MSFT0101 published;')
    require(text,
            '[VTPM-CORE] PASS:', '[VTPM-NV] PASS:',
            '[VTPM-RNG] PASS: RDRAND source',
            '[VTPM-CLOCK] PASS: calibrated TSC; mask=0x1F',
            '[VTPM-PLAT] PASS: platform callbacks; mask=0x1F',
            '[VTPM-CMD] PASS: ExecuteCommand; mask=0x3F stage=0',
            'SMM core protocol installation: Success',
            'PASS: shared buffer and bounds checks verified',
            'PASS: TPM GetCapability through shared buffer',
            'PASS: TPM GetRandom through shared buffer',
            '[VTPM-FULL] PASS: full buffers, TPM errors and bounds verified',
            '[VTPM-CRB] PASS: diagnostic register model verified',
            '[VTPM-CTRL] PASS: snapshot, deferred execution and recovery verified',
            '[VTPM-SMI] PASS: all ' + str(args.cpus) + ' CPUs in SMM before timer worker',
            '[VTPM-WIN] PASS: automatic deferred SMI completion')
    if '[VTPM-WIN] FAIL:' in text or '[VTPM-CTRL] FAIL:' in text:
        raise RuntimeError('Unexpected transport failure')
    text = boot(args, 'no-rdrand-' + args.accel + '-' + str(args.cpus) + 'cpu',
                'FAIL: TPM shared-buffer test=5 result=4', rdrand=False)
    require(text, '[VTPM-RNG] FAIL: RDRAND source',
            '[VTPM-CMD] FAIL: ExecuteCommand; mask=0x0 stage=3')
    if 'TPM2/MSFT0101 published;' in text:
        raise RuntimeError('An unavailable core was advertised to the OS')
    print('Firmware checks passed. Windows/TBS is a separate guest test.')


if __name__ == '__main__':
    main()
