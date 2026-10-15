# TPM2.0-UEFI-STUB

**An experimental TPM 2.0 firmware emulator for EDK II / OVMF on QEMU q35.**

The Microsoft/TCG TPM reference core runs in System Management Mode (SMM).
Windows discovers an ACPI `MSFT0101` device and sends commands through its normal
TPM driver and TPM Base Services (TBS). No QEMU `-tpmdev` backend is used.

**Status: development paused at a working Windows TBS milestone.** Windows boots,
Device Manager reports that the device works normally, and the supplied TBS
probe passes GetCapability and GetRandom. This is a research prototype, not a
complete or certified TPM implementation.

## Current capabilities and limits

| Working / tested | Not completed |
| --- | --- |
| Microsoft/TCG core initialized once in SMM | Persistent NV, seeds, and keys across firmware restarts |
| ACPI TPM2 table, `MSFT0101`, ACPI StartMethod=2 | Full Windows provisioning, PPI/MOR, measured boot / TCG2 integration |
| Private SMRAM command snapshots and fixed shared buffers | ECC and several optional algorithm/profile features |
| Deferred SMI execution with all-vCPU rendezvous | Physical motherboard support, suspend/resume, migration |
| Windows TBS device discovery, context, GetCapability, GetRandom | Complete cancellation/timeout coverage for long commands |
| Positive and negative firmware regression checks | Certification and security review |

**NV is RAM-only. Restarting the firmware loses TPM state and keys.** This build
is not suitable for protecting persistent data with BitLocker or other TPM-bound
keys. A small residual boot stutter was reported in the working Windows test.

## How it works

```text
Windows application → TBS → tpm.sys → ACPI Start _DSM
    → short SMI: validate and snapshot command into SMRAM
    → QEMU software-SMI timer → gather all vCPUs into SMM
    → Microsoft/TCG TPM core → shared response → Windows
```

The Windows-facing interface uses ACPI StartMethod **2**. The separate CRB register
model is a diagnostic experiment, not the OS-facing MMIO implementation.

| Location | Role |
| --- | --- |
| `TpmCoreProbeSmm.c`, `TpmCoreLib/` | Initialize and own the single TPM core; boot self-checks; private SMM protocol |
| `VtpmProbeDxe.c`, `VtpmWindowsDxe.c` | Allocate shared memory, run transport probes, publish ACPI tables |
| `VtpmProbeSmm.c`, `VtpmControlSmm.c` | Validate requests, copy into SMRAM, submit to the core, publish completion |
| `VtpmTimerSmm.c`, `VtpmSmmCpuFeaturesLib/` | Deferred execution and early SMI IPIs for safe all-vCPU rendezvous |
| `VtpmWindows.asl`, `VtpmRuntimeProbe.asl` | Windows TPM device and separate diagnostic ACPI device |
| `scripts/`, `integration/`, `Tests/` | Setup, build/run helpers, OVMF include files, regression and TBS probes |
| `archive/`, `docs/history/` | Superseded experiments and original bring-up notes |

## Build

The supported baseline is **EDK II commit
`49fa59e82e4c6ea798f65fc4e5948eae63ad6e07`** (`vUDK2018-214-g49fa59e82e`).
It uses GCC5 and CryptoPkg's OpenSSL 1.1.0h. Current EDK II is not a drop-in
replacement. The library contains its own recorded TPM source snapshot; a
separate `ms-tpm-20-ref` checkout is not required to build.

On a Linux host, with Git, Docker, and Python 3.8+ installed:

```sh
# From this repository's root, with EDK II in a separate sibling directory:
git clone https://github.com/tianocore/edk2.git ../edk2
git -C ../edk2 checkout 49fa59e82e4c6ea798f65fc4e5948eae63ad6e07
git -C ../edk2 submodule update --init --recursive
export EDK2_DIR="$(cd ../edk2 && pwd)"

python3 scripts/integrate.py --edk2 "$EDK2_DIR" --dry-run
python3 scripts/integrate.py --edk2 "$EDK2_DIR"
docker build -t vtpm-edk2-2018 -f scripts/Dockerfile .
scripts/build.sh --docker
```

The installer applies narrow, reversible DSC/FDF edits and creates a
`MyTpm2EMU` symlink in EDK II when this repository is external. It refuses to
replace another package. Keep the repository at that path while building.

The default build is X64 / DEBUG / GCC5 with `SMM_REQUIRE=TRUE`. Output is under
`$EDK2_DIR/Build/OvmfX64/DEBUG_GCC5/FV/`. See [BUILD.md](docs/BUILD.md) for native
builds, overrides, ACPI regeneration, and removing the integration.

## Run in QEMU

Use a QEMU version exposing `ICH9-LPC.x-smi-swsmi-timer`; the working Windows
milestone used QEMU 11.1.1. QEMU 11.1.2 was present during repository cleanup.
The launcher explicitly enables SMM and SMI broadcast and disables S3.

```sh
# Inspect the command without starting a VM:
python3 scripts/run-qemu.py --accel kvm --dry-run

# Boot firmware without a guest disk:
python3 scripts/run-qemu.py --accel kvm

# Attach your existing Windows installation (raw format in this example):
python3 scripts/run-qemu.py --accel kvm --disk /path/to/windows.img
```

KVM requires host support and access to `/dev/kvm`; TCG is available with
`--accel tcg`. Use `--disk-format qcow2` for qcow2 images. No disk image or
Windows installation is included. QEMU can write to the attached guest disk.

UEFI VARS is a fresh temporary copy by default. Use
`--vars-file /path/to/guest-vars.fd` to retain UEFI variables; this does **not**
make the emulated TPM's RAM NV persistent. Firmware logs go to
`artifacts/qemu/`. Do not attach a second QEMU TPM device.

**Keep `ICH9-LPC.x-smi-broadcast=on` and the custom CPU features library.**
The earlier broadcast-off workaround caused a runtime-variable race and was
replaced. See [troubleshooting](docs/TROUBLESHOOTING.md).

## Verify

```sh
python3 scripts/verify-sources.py
python3 -m unittest discover -s Tests -p 'test_*.py'

# After building firmware; these tests attach no guest disks:
python3 Tests/run-windows-transport-smoke.py --accel kvm
python3 Tests/run-smi-latency.py --accel kvm
python3 Tests/run-windows-no-timer.py
```

Inside the Windows guest, copy `Tests/Test-VtpmTbs.ps1` and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Test-VtpmTbs.ps1
```

Expected: TPM version 2, manufacturer `VTPM`, GetCapability PASS, GetRandom PASS,
and `PASS: Windows TBS -> tpm.sys -> ACPI -> SMM -> TPM core`.
Firmware-only smoke tests do not replace this Windows test.

## Documentation

- [Build and integration](docs/BUILD.md)
- [Architecture and module map](docs/ARCHITECTURE.md)
- [Validation and evidence](docs/VALIDATION.md)
- [Test commands](Tests/README.md)
- [Known problems and troubleshooting](docs/TROUBLESHOOTING.md)
- [Paused development plan / remaining work](PLAN.md)
- [Original development notes](docs/history/README.md)

## License and credits

Original project code and documentation are licensed under [MIT](LICENSE).
Imported Microsoft/TCG, EDK II, and OpenSSL files retain their respective licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Built on the [Microsoft TPM reference implementation](https://github.com/microsoft/ms-tpm-20-ref)
and [TianoCore EDK II](https://github.com/tianocore/edk2).
