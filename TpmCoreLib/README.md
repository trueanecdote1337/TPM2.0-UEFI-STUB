# TPM core library

This EDK II `BASE` library is linked by `TpmCoreProbeSmm` only. It wraps a vendored
Microsoft/TCG TPM 2.0 reference implementation in a firmware-compatible platform
layer. Transports reach that one core through the private SMM protocol; they do
not link separate copies of the core.

| Directory | Purpose |
| --- | --- |
| `Vendor/` | Unmodified imported TPM source snapshot: 487 files |
| `Port/` | Firmware callbacks and reference-derived code requiring adaptation |
| `Config/` | Firmware algorithm/build profile |
| `Compat/` | EDK-compatible CRT and type glue |
| `EdkHeaders/` | 106 copied EDK/OpenSSL headers, recorded in a hash manifest |
| `Include/` | Core facade, probe declarations, private SMM protocol |

`Port/TpmRun.c` owns initialization (`TPM_Manufacture`, `_TPM_Init`), serialized
submission, private request/response buffers, and the SetJump/LongJump boundary
for TPM failures. It does not recover arbitrary CPU exceptions.

Platform backends implement volatile 16 KiB NV, bounded RDRAND entropy, a TSC
clock calibrated against the ACPI timer, PCR defaults, metadata, and power /
locality / cancel callbacks. No runtime Boot Services calls are used by these
backends. NV commit currently commits to RAM only: seeds and keys are regenerated
after firmware restart.

The profile disables ECC, Camellia, CertifyX509, ACT, FW/SVN-limited features,
simulation, and debug RNG. The old CryptoPkg OpenSSL backend is a known constraint.
Self-tests remain enabled. The actual command/response maxima come from the
compiled core; the current profile advertises 3968 bytes in each direction.

Run `python3 scripts/verify-sources.py` from the repository root to verify both
imported snapshots. Original license notices are preserved; see
[third-party notices](../THIRD_PARTY_NOTICES.md). The detailed chronological port
notes are archived in [docs/history](../docs/history/README.md).

See [architecture](../docs/ARCHITECTURE.md), [validation](../docs/VALIDATION.md),
and [remaining work](../PLAN.md) for the supported state of this port.
