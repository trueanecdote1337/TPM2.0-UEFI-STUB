#!/usr/bin/env python3
"""Check fast traditional SMI delivery and reject unsafe relaxed CPU synchronization."""
import re
from qemu_support import boot, configure, parser, require


def main():
    args = configure(parser(__doc__).parse_args())
    text = boot(args, 'smi-traditional-' + args.accel, '[VTPM-WIN] PASS: TPM2/MSFT0101 published;')
    require(text, '[VTPM-SMI] CPU sync snapshot: traditional',
            '[VTPM-SMI] PASS: all ' + str(args.cpus) + ' CPUs in SMM before timer worker')
    match = re.search(r'deferred round-trip=(\d+) us', text)
    if match is None or int(match.group(1)) > 250000:
        raise RuntimeError('Deferred GetRandom did not meet the 250 ms boot-probe limit')
    print('Deferred GetRandom: ' + match.group(1) + ' us')
    text = boot(args, 'smi-relaxed-rejected-' + args.accel,
                '[VTPM-WIN] FAIL: deferred worker unavailable or slow;', broadcast=False)
    require(text, '[VTPM-SMI] Refusing relaxed CPU sync;')
    if 'TPM2/MSFT0101 published;' in text:
        raise RuntimeError('Unsafe CPU policy was advertised to the OS')


if __name__ == '__main__':
    main()
