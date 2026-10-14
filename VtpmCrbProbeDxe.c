#include <Uefi.h>
#include <Protocol/SmmControl2.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include "VtpmCrb.h"

STATIC UINT32 Be32(CONST UINT8 *Data)
{
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}
STATIC BOOLEAN Kick(EFI_SMM_CONTROL2_PROTOCOL *Control)
{
  UINT8 Command = VTPM_PROBE_SMI_COMMAND;
  UINT8 Data = VTPM_CRB_KICK;
  EFI_STATUS Status = Control->Trigger(Control, &Command, &Data, FALSE, 0);
  return !EFI_ERROR(Status) && IoRead8(VTPM_PROBE_APM_DATA_PORT) == VTPM_PROBE_SMI_ACK;
}
EFI_STATUS EFIAPI VtpmCrbTransportTest(EFI_PHYSICAL_ADDRESS Base, EFI_SMM_CONTROL2_PROTOCOL *Control)
{
  STATIC CONST UINT8 Cap[] = {0x80,1,0,0,0,22,0,0,1,0x7a,0,0,0,0,0,0,0,1,0,0,0,32};
  STATIC CONST UINT8 Rand[] = {0x80,1,0,0,0,12,0,0,1,0x7b,0,32};
  volatile PTP_CRB_REGISTERS *Regs = (volatile PTP_CRB_REGISTERS *)(UINTN)(Base + VTPM_CRB_OFFSET);
  UINT8 *Cmd = (UINT8 *)(UINTN)(Base + VTPM_CRB_COMMAND_OFFSET);
  UINT8 *Rsp = (UINT8 *)(UINTN)(Base + VTPM_CRB_RESPONSE_OFFSET);
  UINT32 Step = 1;
  UINT32 Capacity = Regs->CrbControlCommandSize;
  UINTN Index;
  if (!(Regs->LocalityState & PTP_CRB_LOCALITY_STATE_TPM_REG_VALID_STATUS) ||
      Regs->LocalityStatus != 0 || Regs->CrbControlStatus != PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE ||
      Capacity < 10 || Capacity > VTPM_FULL_CAPACITY ||
      Regs->CrbControlResponseSize < 10 || Regs->CrbControlResponseSize > VTPM_FULL_CAPACITY) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: layout and initial idle\n"));
  Step = 2;
  Regs->LocalityControl = PTP_CRB_LOCALITY_CONTROL_REQUEST_ACCESS;
  if (!Kick(Control) || Regs->LocalityControl != 0 ||
      Regs->LocalityStatus != PTP_CRB_LOCALITY_STATUS_GRANTED ||
      !(Regs->LocalityState & PTP_CRB_LOCALITY_STATE_LOCALITY_ASSIGNED)) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: locality 0 granted\n"));
  Step = 3;
  Regs->CrbControlRequest = PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY;
  if (!Kick(Control) || Regs->CrbControlRequest != 0 || Regs->CrbControlStatus != 0) goto Fail;
  CopyMem(Cmd, Cap, sizeof(Cap)); Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 || Regs->CrbControlStatus != 0 ||
      Be32(Rsp + 6) != 0 || Be32(Rsp + 2) <= 64 || Be32(Rsp + 2) > Regs->CrbControlResponseSize) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: ready/start/large response/completion\n"));
  Step = 4;
  Regs->CrbControlCommandAddressLow = MAX_UINT32;
  Regs->CrbControlCommandAddressHigh = MAX_UINT32;
  Regs->CrbControlResponseAddrss = 0;
  Regs->CrbControlCommandSize = MAX_UINT32;
  Regs->CrbControlResponseSize = MAX_UINT32;
  CopyMem(Cmd, Rand, sizeof(Rand)); Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 || Regs->CrbControlStatus != 0 ||
      Be32(Rsp + 6) != 0 || Be32(Rsp + 2) != 44 ||
      Regs->CrbControlCommandSize != Capacity ||
      Regs->CrbControlCommandAddressLow != (UINT32)(Base + VTPM_CRB_COMMAND_OFFSET) ||
      Regs->CrbControlCommandAddressHigh != (UINT32)((Base + VTPM_CRB_COMMAND_OFFSET) >> 32) ||
      Regs->CrbControlResponseAddrss != Base + VTPM_CRB_RESPONSE_OFFSET) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: buffer addresses and capacities pinned\n"));
  Step = 5;
  Regs->CrbControlRequest = PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE;
  if (!Kick(Control) || Regs->CrbControlStatus != PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE) goto Fail;
  SetMem(Rsp, 16, 0xa5); Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 ||
      Regs->CrbControlStatus != PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE) goto Fail;
  for(Index = 0; Index < 16; ++Index) if(Rsp[Index] != 0xa5) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: idle blocks execution\n"));
  Step = 6;
  Regs->CrbControlRequest = PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY;
  Regs->CrbControlCancel = 1; Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 || Be32(Rsp + 6) != 0x909 || Be32(Rsp + 2) != 10) goto Fail;
  Regs->CrbControlCancel = 0; Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 || Be32(Rsp + 6) != 0 || Be32(Rsp + 2) != 44) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: cancel before execution and next command\n"));
  Step = 7;
  Cmd[2] = 0; Cmd[3] = 0; Cmd[4] = 0x20; Cmd[5] = 0;
  Regs->CrbControlStart = 1;
  if (!Kick(Control) || Regs->CrbControlStart != 0 || Be32(Rsp + 6) != 0x142 || Be32(Rsp + 2) != 10) goto Fail;
  CopyMem(Cmd, Rand, sizeof(Rand)); Regs->CrbControlStart = 1;
  if (!Kick(Control) || Be32(Rsp + 6) != 0 || Be32(Rsp + 2) != 44) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: invalid command size and recovery\n"));
  Step = 8;
  Regs->LocalityControl = PTP_CRB_LOCALITY_CONTROL_RELINQUISH;
  if (!Kick(Control) || Regs->LocalityStatus != 0 ||
      (Regs->LocalityState & PTP_CRB_LOCALITY_STATE_LOCALITY_ASSIGNED) ||
      Regs->CrbControlStatus != PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE) goto Fail;
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: relinquish and idle\n"));
  DEBUG((DEBUG_INFO, "[VTPM-CRB] PASS: diagnostic register model verified\n"));
  return EFI_SUCCESS;
Fail:
  DEBUG((DEBUG_ERROR, "[VTPM-CRB] FAIL: step=%u status=0x%x start=%u locality=0x%x\n",
         Step, Regs->CrbControlStatus, Regs->CrbControlStart, Regs->LocalityStatus));
  return EFI_DEVICE_ERROR;
}
