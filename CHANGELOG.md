# Change log

## Publication preparation — 2026-10-15

- Freeze development at the user-confirmed Windows TBS milestone.
- Replace chronological READMEs with current English documentation and a resume plan.
- Preserve original notes, inactive stub, and intermediate tests in history/archive.
- Add reversible integration for the pinned EDK II baseline and portable build/run scripts.
- Consolidate disk-free QEMU regression runners with explicit firmware paths and cleanup.
- Verify imported source manifests and embedded ACPI arrays; add installer rollback tests.
- Align the CPU relocation-complete hook with the supported 2018 EDK baseline;
  no new TPM command functionality or persistent storage is added.
- Add MIT for original project work and retain third-party license notices.
- Keep raw logs and build outputs as ignored local artifacts rather than source files.

## Working Windows checkpoint — 2026-10-06

- Windows TPM device reports normal status under QEMU.
- TBS device discovery/context, GetCapability, and GetRandom pass.
- Traditional SMM rendezvous with early AP IPIs fixes the runtime-variable race.
- NV remains volatile; provisioning and physical platform support remain unfinished.
