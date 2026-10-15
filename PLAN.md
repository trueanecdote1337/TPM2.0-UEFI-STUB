# Project status and resume plan

**Development paused on 2026-10-15.** The current checkpoint is a working
Windows TPM driver / TBS connection under QEMU.

## Completed and validated

- [x] Microsoft/TCG core ported into a single SMM owner.
- [x] Firmware crypto/profile glue, volatile NV, entropy, clock, PCR/platform callbacks.
- [x] Real Startup, SelfTest, GetCapability, GetRandom and error-response boot probes.
- [x] Legacy/full diagnostic channels and CRB register-model probes.
- [x] Windows Control Area, independent buffers, standard ACPI StartMethod=2.
- [x] Deferred SMI execution with early AP IPIs and traditional all-vCPU rendezvous.
- [x] Windows loads to the desktop; TPM device reports normal status.
- [x] Windows TBS device info, context, GetCapability and GetRandom pass.
- [x] Negative checks for missing RDRAND, missing timer, and unsafe relaxed sync.
- [x] Publication helpers, portable script paths, English documentation and MIT license
  for original code; third-party license notices preserved.

## Resume here

1. **Design persistent NV.** Select storage suitable for firmware/SMM, define atomic
   commit/recovery and initialization rules, and preserve hierarchy seeds and keys
   across reboots. Do not treat the current RAM commit as persistence.
2. Test NV Define/Write/Read/Undefine and object create/load/sign/flush through TBS;
   verify persistence and interrupted-write recovery after implementing storage.
3. Measure long-running RSA operations and residual SMM stutter. Add a tested
   cancellation/timeout strategy; avoid unbounded OS pauses inside SMM.
4. Implement and validate PPI/MOR, TCG2/measured boot, and Windows provisioning.
5. Revisit disabled algorithms/features, particularly ECC and the old crypto backend.
6. Port the transport/timer to a physical platform only as a separate integration;
   validate runtime variables, CPU rendezvous, suspend/resume, and failure handling.

## Preserve these decisions

- One core instance; transport drivers call its private SMM protocol.
- Snapshot external requests into SMRAM; never follow OS-modified buffer pointers.
- Use ACPI StartMethod=2. The diagnostic CRB is not the Windows-facing interface.
- Keep `x-smi-broadcast=on` and `VtpmSmmCpuFeaturesLib`. The earlier relaxed-sync
  workaround caused a Windows runtime-variable ASSERT and was superseded.
- Sample dynamic DXE PCD policy before ReadyToLock; do not access that database
  from runtime SMM.
- Keep original imported sources intact and verify their manifests.

Current evidence and limitations: [docs/VALIDATION.md](docs/VALIDATION.md).
Detailed chronological notes: [docs/history](docs/history/README.md).
