#ifndef VTPM_CRB_H_
#define VTPM_CRB_H_
#include <Uefi.h>
#include <IndustryStandard/TpmPtp.h>
#include <VtpmCoreSmm.h>
#include "VtpmProbe.h"
#define VTPM_CRB_OFFSET 0x4000
#define VTPM_CRB_KICK 0x52
#define VTPM_CRB_HEADER_SIZE 0x80
#define VTPM_CRB_COMMAND_OFFSET (VTPM_FULL_OFFSET + OFFSET_OF(VTPM_FULL_PACKET, Request))
#define VTPM_CRB_RESPONSE_OFFSET (VTPM_FULL_OFFSET + OFFSET_OF(VTPM_FULL_PACKET, Response))
typedef CHAR8 VTPM_CRB_LAYOUT_CHECK[
 (OFFSET_OF(PTP_CRB_REGISTERS, CrbControlRequest) == 0x40 &&
  OFFSET_OF(PTP_CRB_REGISTERS, CrbControlStart) == 0x4c &&
  OFFSET_OF(PTP_CRB_REGISTERS, CrbControlCommandSize) == 0x58 &&
  OFFSET_OF(PTP_CRB_REGISTERS, CrbControlResponseAddrss) == 0x68 &&
  OFFSET_OF(PTP_CRB_REGISTERS, CrbDataBuffer) == VTPM_CRB_HEADER_SIZE &&
  VTPM_CRB_OFFSET + sizeof(PTP_CRB_REGISTERS) <= VTPM_SHARED_SIZE) ? 1 : -1];
VOID VtpmCrbBind(EFI_PHYSICAL_ADDRESS Address);
VOID VtpmCrbProcess(BOOLEAN AfterEbs);
#endif
