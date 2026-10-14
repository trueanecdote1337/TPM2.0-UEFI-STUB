#include <Uefi.h>
#include <Protocol/SmmControl2.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include "VtpmProbe.h"

STATIC UINT32 ReadBe32 (CONST UINT8 *Buffer)
{
  return ((UINT32)Buffer[0] << 24) | ((UINT32)Buffer[1] << 16) |
         ((UINT32)Buffer[2] << 8) | Buffer[3];
}

EFI_STATUS EFIAPI
VtpmFullTransportTest (EFI_PHYSICAL_ADDRESS SharedAddress, EFI_SMM_CONTROL2_PROTOCOL *Control)
{
  STATIC CONST UINT8 Cap[] = {0x80,1,0,0,0,22,0,0,1,0x7a,0,0,0,0,0,0,0,1,0,0,0,32};
  STATIC CONST UINT8 Rand[] = {0x80,1,0,0,0,12,0,0,1,0x7b,0,64};
  STATIC CONST UINT8 Unknown[] = {0x80,1,0,0,0,10,0xff,0xff,0xff,0xff};
  VTPM_FULL_PACKET *Packet = (VTPM_FULL_PACKET *)(UINTN)(SharedAddress + VTPM_FULL_OFFSET);
  UINT8 *Guard = (UINT8 *)(Packet + 1);
  EFI_STATUS Status;
  UINT32 Test;
  UINT32 ExpectedResult;
  UINT32 ExpectedRc;
  UINT32 Index;
  UINT8 Command;
  UINT8 Data;
  BOOLEAN Valid;

  for (Test = 1; Test <= 9; ++Test) {
    ZeroMem (Packet, sizeof (*Packet));
    SetMem (Guard, 16, 0xa5);
    Packet->Signature = VTPM_FULL_SIGNATURE;
    Packet->Version = VTPM_FULL_VERSION;
    Packet->Sequence = Test;
    Packet->ResponseCapacity = VTPM_FULL_CAPACITY;
    Packet->RequestSize = Test == 1 ? sizeof (Cap) : (Test == 3 ? sizeof (Unknown) : sizeof (Rand));
    CopyMem (Packet->Request, Test == 1 ? Cap : (Test == 3 ? Unknown : Rand), Packet->RequestSize);
    if (Test == 4) Packet->Request[5] = 11;
    if (Test == 5) Packet->RequestSize = VTPM_FULL_CAPACITY + 1;
    if (Test == 6) Packet->ResponseCapacity = 64;
    if (Test == 7) Packet->ResponseCapacity = VTPM_FULL_CAPACITY + 1;
    if (Test == 8) Packet->RequestSize = 5;
    // Test 9 proves normal command handling still works after all errors.
    Packet->Result = MAX_UINT32;
    Packet->State = VTPM_PACKET_READY;
    Command = VTPM_PROBE_SMI_COMMAND;
    Data = VTPM_PROBE_SMI_REQUEST;
    Status = Control->Trigger (Control, &Command, &Data, FALSE, 0);
    ExpectedResult = Test >= 5 && Test <= 7 ? 3 : 0;
    ExpectedRc = Test == 3 ? 0x143 : (Test == 4 ? 0x142 : (Test == 8 ? 0x9a : 0));
    Valid = !EFI_ERROR (Status) && IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_PROBE_SMI_ACK &&
            Packet->State == VTPM_PACKET_DONE && Packet->ResponseSequence == Test &&
            Packet->Result == ExpectedResult && Packet->ResponseSize <= VTPM_FULL_CAPACITY;
    for (Index = 0; Index < 16; ++Index) {
      if (Guard[Index] != 0xa5) Valid = FALSE;
    }
    if (ExpectedResult == 0) {
      Valid = Valid && Packet->ResponseSize >= 10 &&
              ReadBe32 (&Packet->Response[2]) == Packet->ResponseSize &&
              ReadBe32 (&Packet->Response[6]) == ExpectedRc;
      if (Test == 1) {
        Valid = Valid && Packet->ResponseSize > 64 && Packet->ResponseSize >= 19 &&
                ReadBe32 (&Packet->Response[11]) == 0 &&
                ReadBe32 (&Packet->Response[15]) <= 32 &&
                Packet->ResponseSize == 19 + 6 * ReadBe32 (&Packet->Response[15]);
      }
      if (Test == 2 || Test == 9) {
        // TPM may return fewer bytes than requested (current profile: 48).
        Valid = Valid && Packet->ResponseSize > 12 && Packet->ResponseSize <= 76 &&
                Packet->Response[10] == 0 &&
                Packet->Response[11] == Packet->ResponseSize - 12;
      }
    } else {
      Valid = Valid && Packet->ResponseSize == 0;
    }
    if (!Valid) {
      DEBUG ((DEBUG_ERROR, "[VTPM-FULL] FAIL: test=%u result=%u response=%u rc=0x%x status=%r\n",
              Test, Packet->Result, Packet->ResponseSize, ReadBe32 (&Packet->Response[6]), Status));
      return EFI_DEVICE_ERROR;
    }
    DEBUG ((DEBUG_INFO, "[VTPM-FULL] PASS: test=%u result=%u response=%u\n",
            Test, Packet->Result, Packet->ResponseSize));
  }
  DEBUG ((DEBUG_INFO, "[VTPM-FULL] PASS: full buffers, TPM errors and bounds verified\n"));
  return EFI_SUCCESS;
}
