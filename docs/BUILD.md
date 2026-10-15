# Build and integration

## Supported baseline

- Linux host; Python 3.8+ for the project scripts.
- EDK II `49fa59e82e4c6ea798f65fc4e5948eae63ad6e07`.
- Its OpenSSL submodule initialized at `OpenSSL_1_1_0h`.
- Ubuntu 16.04 / GCC 5 / Python 2 for the historical EDK build tools.
- X64 OVMF, DEBUG, `SMM_REQUIRE=TRUE`, default 4 MiB flash layout.
- QEMU q35 with SMM, RDRAND, and software-SMI timer support.

The historical compiler environment is deliberate. This package has copied EDK /
OpenSSL headers and compatibility glue for that baseline. Supporting a new EDK II
release requires an actual port and regression testing, not just a version change.

## Install and build

Follow the root README to check out EDK II and build the Docker image. The package
may live inside EDK II as `MyTpm2EMU/`, or outside it with an integration symlink.
`EDK2_DIR` must identify the EDK checkout, not the vTPM repository.

```sh
export EDK2_DIR=/absolute/path/to/edk2
python3 scripts/integrate.py --edk2 "$EDK2_DIR" --dry-run
python3 scripts/integrate.py --edk2 "$EDK2_DIR"
scripts/build.sh --docker
```

Integration adds three modules to both DSC and FDF, maps `TpmCoreLib` for SMM
modules, and selects the project's CPU features library for `PiSmmCpuDxeSmm`.
Repeated installation is idempotent. Both files are validated before writing;
unrelated settings and CRLF line endings are preserved. An existing unrelated
`MyTpm2EMU` directory is never overwritten.

`build.sh` also checks/installs integration before building. Docker mounts the
repository at its real path so an external symlink resolves inside the container.
It runs as the invoking UID/GID. Firmware and BaseTools output are written in the
EDK II checkout. It does not change ownership of the checkout or use sudo.

Overrides:

```sh
VTPM_JOBS=8 scripts/build.sh --docker
VTPM_BUILD_IMAGE=my-existing-legacy-image scripts/build.sh --docker
# Additional EDK build arguments are forwarded:
scripts/build.sh --docker -D SECURE_BOOT_ENABLE=TRUE
```

Extra flags have not all been validated together. Do not disable SMM or select a
second TPM implementation. A host environment already configured with compatible
GCC5/Python2/BaseTools can use `scripts/build.sh` without `--docker`.

The historical Ubuntu image depends on package archive availability. The
Dockerfile is included for rebuilding the environment, but it is not an exported
or immutable builder image. Reusing a compatible existing image is supported.

## Generated ACPI files

`VtpmProbeDxe` includes `VtpmRuntimeProbe.hex` and `VtpmWindows.hex` directly.
Normal EDK builds use the checked-in arrays and do not regenerate these ASL files.
The checked-in artifacts were generated with ACPICA **20251212**.

After editing ASL:

```sh
python3 scripts/regenerate-acpi.py
python3 scripts/verify-sources.py
python3 Tests/check-acpi-mailbox.py
python3 Tests/check-windows-acpi.py
```

For a reproducibility check with the same compiler version:

```sh
python3 scripts/regenerate-acpi.py --check
```

`--check` compares AML bytes and validates each checked-in HEX/AML pair. Different
compiler versions can change headers or AML encoding; review and regenerate both
files together. Install `iasl` and `acpiexec` on the host for these checks. Old
ACPICA bundled in the builder is sufficient for its EDK tables, but is not the
reference version for regenerating the project's checked-in ACPI artifacts.

## Remove integration

```sh
python3 scripts/integrate.py --edk2 "$EDK2_DIR" --remove --dry-run
python3 scripts/integrate.py --edk2 "$EDK2_DIR" --remove
```

This removes only recognized integration edits and the symlink created for this
repository. It does not delete an in-tree clone, built firmware, VARS files, or
other EDK II changes. Rebuild OVMF afterward to remove vTPM from the firmware.
