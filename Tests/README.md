# Tests

Run these scripts from the repository root with host Python 3.8+. Python in the
legacy firmware builder is not the environment for these host-side runners.

## Source and integration checks

```sh
python3 scripts/verify-sources.py
python3 -m unittest discover -s Tests -p 'test_*.py'
```

The first verifies 487 vendored TPM files, 106 copied headers, and the two embedded
HEX/AML pairs. Integration tests use temporary fixtures to check idempotence,
reversibility, preservation of CRLF/unrelated changes, rejection of unknown
edits, and rollback after a simulated second-file write failure.

## Firmware-only QEMU checks

Build the firmware first, then set `EDK2_DIR` or pass an explicit FV directory:

```sh
python3 Tests/run-windows-transport-smoke.py --fv-dir /path/to/FV --accel tcg
python3 Tests/run-windows-transport-smoke.py --fv-dir /path/to/FV --accel kvm --cpus 4
python3 Tests/run-smi-latency.py --fv-dir /path/to/FV --accel kvm
python3 Tests/run-windows-no-timer.py --fv-dir /path/to/FV
```

All use fresh temporary UEFI VARS and attach **no guest disks**. Logs are under
`artifacts/tests/` by default; `--log-dir` overrides that. `--timeout` defaults to
60 seconds per boot. The runner terminates QEMU and cleans temporary VARS even
on timeout/interruption, and reports process errors instead of accepting old logs.
`VTPM_FV_DIR`, `VTPM_SMOKE_ACCEL`, and `VTPM_SMOKE_CPUS` are also supported.

| Script | Coverage |
| --- | --- |
| `run-windows-transport-smoke.py` | Core/platform checks, legacy/full/CRB/control paths, all-CPU timer rendezvous, ACPI publication; negative CPU without RDRAND |
| `run-smi-latency.py` | Traditional CPU sync, deferred GetRandom <=250 ms, refusal of relaxed CPU sync |
| `run-windows-no-timer.py` | Core remains available but TPM is not published when its timer is disabled |

Expected negative cases deliberately contain FAIL markers. The runner checks the
specific expected failure and rejects ASSERT, SMM memory violations, or CPU
exceptions. A PASS filename or successful QEMU startup alone is not sufficient.
The 250 ms threshold applies to the boot GetRandom probe, not to all TPM commands.

## ACPI checks

With host `iasl` and `acpiexec` available:

```sh
python3 Tests/check-acpi-mailbox.py
python3 Tests/check-windows-acpi.py
```

These validate SystemMemory access types, execute AML under simulated regions,
and check HID / memory-only resources / `_DSM` queries and failed-start behavior.
They cannot generate a real SMI. Compiler 20251212 produced the tracked firmware
AML; the execution checks also tolerate the output formatting of ACPICA 20160108.
Do not regenerate committed AML with an older compiler without reviewing the diff.

## Windows guest test

Copy `Test-VtpmTbs.ps1` into a booted Windows guest and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Test-VtpmTbs.ps1
```

The probe calls `tbs.dll` to read device info, create a TPM 2.0 context, and submit
GetCapability(manufacturer) / GetRandom. It validates both TBS status and the TPM
response. It does not create or clear keys, define NV indices, or change TPM
ownership. It closes its context in `finally`.

The user confirmed this probe passes under Windows/QEMU. Host firmware tests do
not boot Windows or independently verify guest TBS. See
[validation](../docs/VALIDATION.md) for that distinction.

Historical incremental scripts are in `archive/tests/`; use the entry points above.
