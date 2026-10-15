#!/usr/bin/env python3
"""An unavailable SMI timer must not expose a dead TPM device to the guest."""
from qemu_support import boot, configure, parser, require


def main():
    args = configure(parser(__doc__).parse_args())
    text = boot(args, 'no-timer-' + args.accel,
                '[VTPM-WIN] FAIL: deferred worker unavailable or slow;', timer=False)
    require(text, '[VTPM-CTRL] PASS: snapshot, deferred execution and recovery verified')
    if 'TPM2/MSFT0101 published;' in text:
        raise RuntimeError('A nonfunctional timer was advertised to the OS')


if __name__ == '__main__':
    main()
