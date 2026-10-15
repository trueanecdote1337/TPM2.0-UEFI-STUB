#ifndef VTPM_CONTROL_H_
#define VTPM_CONTROL_H_
#include <Uefi.h>
#include <IndustryStandard/Tpm2Acpi.h>
#include <VtpmCoreSmm.h>
#include "VtpmProbe.h"
#define VTPM_CONTROL_OFFSET 0x5000
#define VTPM_CONTROL_COMMAND_OFFSET 0x6000
#define VTPM_CONTROL_RESPONSE_OFFSET 0x7000
// Tokens 0x53/0x54 are diagnostic two-phase entry points.
// The OS ACPI Start method uses token 0x55 and automatic timer execution.
#define VTPM_CONTROL_ACCEPT 0x53
#define VTPM_CONTROL_EXECUTE 0x54
#define VTPM_CONTROL_START 0x55
#define VTPM_CONTROL_STOP 0x56
VOID VtpmTimerInitialize(VOID);
BOOLEAN VtpmTimerArm(VOID);
BOOLEAN VtpmTimerDispatch(BOOLEAN AfterEbs);
VOID VtpmTimerStop(VOID);
VOID VtpmControlAbort(VOID);
#define VTPM_CONTROL_REJECT 0xA6
VOID VtpmControlBind(EFI_PHYSICAL_ADDRESS Address);
BOOLEAN VtpmControlAccept(VOID);
VOID VtpmControlExecute(BOOLEAN AfterEbs);
typedef CHAR8 VTPM_CONTROL_LAYOUT_CHECK[
 (sizeof(EFI_TPM2_ACPI_CONTROL_AREA) == 48 &&
  OFFSET_OF(EFI_TPM2_ACPI_CONTROL_AREA, Start) == 12 &&
  VTPM_CONTROL_RESPONSE_OFFSET + VTPM_FULL_CAPACITY <= VTPM_SHARED_SIZE) ? 1 : -1];
#endif
