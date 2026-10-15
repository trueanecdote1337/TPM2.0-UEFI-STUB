# Troubleshooting

## No TPM device in Windows

Check the build contains all three vTPM modules and the custom CPU library. Look
for these boot markers:

```text
[VTPM-SMI] CPU sync snapshot: traditional
[VTPM-SMI] PASS: all 2 CPUs in SMM before timer worker
[VTPM-WIN] PASS: automatic deferred SMI completion
[VTPM-WIN] PASS: TPM2/MSFT0101 published; StartMethod=2 ...
```

The CPU count reflects the configured VM. A standard OVMF build without package
integration will not provide this vTPM. A core failure, missing timer, unsafe CPU
policy, or slow boot probe intentionally prevents device publication.

## QEMU reports an unknown SMI timer property

Use a version exposing `ICH9-LPC.x-smi-swsmi-timer`. Inspect with:

```sh
qemu-system-x86_64 -device ICH9-LPC,help
```

The working milestone used QEMU 11.1.1. Timer support cannot be replaced by merely
publishing a TPM2 ACPI table. No portable physical-chipset implementation exists.

## Slow boot or runtime-variable ASSERT

Keep **`ICH9-LPC.x-smi-broadcast=on`** and the custom
`VtpmSmmCpuFeaturesLib` mapping. The QEMU software timer delivers a single-CPU SMI;
the early entry hook sends IPIs to the remaining vCPUs before the standard
traditional rendezvous waits for them.

The superseded broadcast-off workaround reduced latency but left an OS vCPU
running while another CPU set the global SMM `InSmm` flag. A concurrent UEFI
runtime-variable call then asserted in `VariableSmmRuntimeDxe.c`. This is why
relaxed mode is now rejected. Do not suppress that ASSERT as a workaround.

A large `deferred round-trip=... us` indicates a delivery/rendezvous issue. The
250 ms boot guard prevents advertising that slow configuration; it is not a
runtime timeout or a cancellation mechanism for RSA operations.

## Device Manager Code 12

The initial SSDT incorrectly claimed platform SMI ports B2/B3 in the TPM `_CRS`.
The current SSDT claims only its three allocated memory pages and accesses the
platform ports through an AML OperationRegion. Rebuild after ASL/HEX changes and
fully restart the guest. Check that the active firmware is the newly built one.
Do not load the archived `tpmdriver.inf` alongside the current drivers.

## PowerShell returns a TBS error

Keep the full error and firmware log. For example, `0x80284001` is
`TBS_E_INTERNAL_ERROR`, not a PowerShell string/type parsing error. A TBS error
alone does not identify a core failure. First check Device Manager and whether
`[VTPM-ACPI] Start accepted` / `[VTPM-CTRL] ... phase=after-EBS` appear.

The diagnostic VPRB messages alone do not prove that the Windows TPM driver ran.

## NV or keys disappear after restart

This is the current implementation: NV is a 16 KiB SMRAM array. Retaining QEMU's
UEFI VARS does not retain the TPM NV array. Persistent storage is the first item
in the resume plan. No claim of usable BitLocker provisioning is made.

## Integration refuses an existing directory or unrecognized edits

The installer will not replace an unrelated `MyTpm2EMU` directory or guess how to
merge a different set of OVMF changes. Use a separate pinned checkout, or inspect
and reconcile the old integration manually. `--dry-run` shows the intended edits.
