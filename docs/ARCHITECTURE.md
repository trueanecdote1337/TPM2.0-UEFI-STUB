# Architecture

## Initialization and command flow

1. `TpmCoreProbeSmmEntryPoint` tests the port, manufactures volatile TPM state,
   starts the TPM, exercises real commands, and publishes a private core protocol.
2. `VtpmProbeDxeEntryPoint` allocates eight contiguous reserved pages below 4 GiB,
   installs the diagnostic SSDT, and registers a ReadyToBoot callback.
3. `VtpmProbeReadyToBoot` binds the shared allocation once through SmmCommunication,
   runs legacy/full/CRB/control probes, verifies deferred execution and CPU
   rendezvous, then calls `VtpmWindowsPublish`.
4. Windows discovers `MSFT0101`. Its standard Start `_DSM` rings the APM doorbell.
5. `VtpmControlAccept` validates the fixed Control Area and copies the command to
   SMRAM. It rejects a second pending submission. It does not execute the command.
6. `VtpmTimerArm` arms the QEMU software-SMI timer. On expiry, the CPU entry hook
   sends early SMI IPIs so the standard traditional rendezvous gathers all CPUs.
7. `VtpmTimerDispatch` disarms the timer; `VtpmControlExecute` submits the private
   snapshot through the core protocol. The response is copied before Start clears.
8. Windows observes completion and reads the response.

Publication is withheld when the core, timer, CPU policy, or boot latency check
fails. Existing TPM2 tables prevent duplicate publication.

## Modules and main entry points

| Module / file | Main functions | Connections |
| --- | --- | --- |
| `TpmCoreProbeSmm.c` | Entry point, platform probes, real command smoke test, core protocol publication | `TpmCoreLib` and private SMM protocol |
| `Port/TpmRun.c` | `VtpmCoreInitialize`, `VtpmCoreSubmit`, `_plat__Fail` | Vendor Manufacture / ExecuteCommand, private buffers, failure boundary |
| `VtpmProbeDxe.c` | DXE entry, shared allocation, ReadyToBoot probes | SmmCommunication, SmmControl2, ACPI table protocol |
| `VtpmWindowsDxe.c` | `VtpmWindowsPublish`, duplicate-table check, checksums / address patching | Embedded `VtpmWindows.hex`, ACPI TPM2 table |
| `VtpmProbeSmm.c` | SMM entry, one-time buffer registration, filtered APM doorbell, diagnostic packet handlers | Shared allocation, core protocol, timer/control/CRB |
| `VtpmControlSmm.c` | Bind, accept, execute, abort | Fixed Control Area, SMRAM snapshots, core protocol |
| `VtpmTimerSmm.c` | Initialize policy snapshot, arm, dispatch, stop | q35 PM registers, all-CPU boot check, control worker |
| `VtpmSmmCpuFeaturesLib/` | CPU initialization and rendezvous-entry adaptation | Standard OVMF save-state support, early SMI IPIs |
| `VtpmCrbSmm.c` | Bind/process locality, ready/idle/start/cancel transitions | Diagnostic CRB page; explicit diagnostic SMI |

The `Probe` names reflect the project's incremental development. Some of these
modules now own runtime functionality; they are not optional test-only drivers.

## Shared memory layout

All offsets are relative to the fixed, once-registered allocation.

| Offset | Use |
| --- | --- |
| `0x0000` | Legacy 64-byte diagnostic request/response mailbox |
| `0x1000` | Full diagnostic packet header and 4 KiB request/response arrays |
| `0x4000` | Diagnostic CRB register page |
| `0x5000` | Windows ACPI Control Area, 48 bytes |
| `0x6000` | Windows command buffer, allocated 4 KiB |
| `0x7000` | Windows response buffer, allocated 4 KiB |

Actual core capacities are currently 3968 bytes. OS-provided physical addresses
are never used to redirect the core's copies. Windows buffers are independent of
the diagnostic full/CRB buffers. The diagnostic CRB model shares the full-packet
buffers and is not intended for concurrent independent callers.

## Platform boundaries

The APM doorbell uses command `0xE7` at B2, with tokens at B3: `0x51` legacy/full,
`0x52` diagnostic CRB, `0x53` accept, `0x54` manual execute, `0x55` standard start,
`0x56` diagnostic stop. Acknowledgements are `0xA5` / `0xA6`.
Both SSDTs serialize AML port writes with the same mutex. Platform-owned APM ports
are accessed through OperationRegion; the TPM `_CRS` claims its three memory pages.

The timer backend is specific to QEMU q35: LPC `00:1f.0`, GEN_PMCON_3 rate selection,
and SMI_EN / SMI_STS bit 6. QEMU's timer interrupt delivery and status semantics
must not be assumed to match a physical ICH9. The CPU entry adaptation targets
QEMU's first CPU at index 0; physical platforms and CPU hotplug are unvalidated.

The custom library retains OVMF save-state handling. The SMM relocation-complete
hook matches the pinned 2018 baseline (no-op); a later imported SEV helper is not
available there. SEV is not a validated configuration for this vTPM.
