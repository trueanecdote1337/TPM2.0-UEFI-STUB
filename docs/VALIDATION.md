# Validation record

## Windows milestone — user-confirmed

The owner confirmed successful Windows boot under QEMU, Device Manager reporting
that the TPM works normally, and `Test-VtpmTbs.ps1` returning:

```text
structVersion:     1
tpmVersion:        2
tpmInterfaceType:  3
tpmImpRevision:    0
GetCapability:     PASS
GetRandom:         PASS
Windows TBS:       PASS
```

The original firmware log `edk2_debug_2026-10-06_01-32-17.log` recorded traditional
CPU sync, six vCPUs present in SMM, a 17652 us boot round trip, successful
TPM2/MSFT0101 publication and after-EBS commands without ASSERT/FAIL. The owner
reconfirmed Windows/QEMU tests on 2026-10-15. A small residual boot stutter was
reported. Exact Windows build information and a standalone TBS transcript were
not recorded, so they are not claimed here.

This proves the tested transport/device/API path. It does not establish complete
Windows provisioning, persistent keys, BitLocker readiness, TPM conformance, or
physical-machine support.

## Repository cleanup — 2026-10-15

The publication layout is checked separately from the historical Windows build:

- Recorded source checksums: all 593 files match (487 TPM + 106 copied headers).
- Both embedded ACPI HEX arrays match the tracked AML files.
- Real-checkout removal restores DSC/FDF byte-for-byte to the pinned originals.
- Four reversible integration fixture tests, Python syntax, Bash syntax, and
  current Markdown file links pass. The launcher boots firmware without a guest
  disk and cleans up after SIGINT.
- A clean, separate EDK II checkout at
  `49fa59e82e4c6ea798f65fc4e5948eae63ad6e07` builds full OVMF X64 DEBUG/GCC5/SMM.
- The builder used Ubuntu 16.04 / GCC 5.4 and the checkout's OpenSSL 1.1.0h.
  The supplied Dockerfile builds as `vtpm-edk2-2018`; the build wrapper also
  completes with that newly built image.
- Positive firmware smoke and missing-RDRAND negative cases pass in QEMU 11.1.2:
  TCG with two vCPUs and KVM with four vCPUs.
- Deferred GetRandom measured 17158 us (TCG/2 vCPUs), 17195 us (KVM/4 vCPUs).
- Traditional/relaxed synchronization regression passes (17152 us in its positive
  KVM case); unsafe relaxed sync is refused. Missing-timer negative test passes.
- ACPICA mailbox and Windows-device execution checks pass using 20160108 tools;
  the checked-in AML was originally generated with 20251212.

The clean build found an imported later SEV relocation API missing from the pinned
2018 EDK baseline. That hook now matches the baseline's no-op behavior. The
ordinary non-SEV QEMU path passes firmware checks; SEV remains unvalidated.
The Windows guest was **not rerun by the packaging session** after this baseline
compatibility adjustment. The Windows results above are owner-reported results
from the working checkpoint.

Fresh firmware checks use temporary VARS and attach no guest disks. Output from
this session is local under ignored `artifacts/`; no disk images, compiled
firmware, or raw logs are required in the source release. Original logs from the
initial source tree are retained locally under `artifacts/legacy-logs/`.

## Unverified / incomplete

- Persistent NV / hierarchy seeds / keys and interrupted-write recovery.
- Broad NV/object/session/policy test coverage through Windows TBS.
- Complete long-command cancellation, timeout behavior, and latency analysis.
- PPI/MOR, measured boot/TCG2, and Windows provisioning requirements.
- ECC and other disabled profile features; current-generation crypto integration.
- Physical machines, CPU hotplug, SEV, S3, resume, or VM migration.

See [PLAN.md](../PLAN.md) for the resume order.
