#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/SmmCommunication.h>
#include <Protocol/SmmControl2.h>
#include <Protocol/AcpiTable.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "VtpmProbe.h"
#include "VtpmRuntimeProbe.hex"

STATIC EFI_GUID mProbeGuid = VTPM_PROBE_GUID;
EFI_STATUS EFIAPI VtpmFullTransportTest (EFI_PHYSICAL_ADDRESS, EFI_SMM_CONTROL2_PROTOCOL *);
EFI_STATUS EFIAPI VtpmCrbTransportTest (EFI_PHYSICAL_ADDRESS, EFI_SMM_CONTROL2_PROTOCOL *);
EFI_STATUS EFIAPI VtpmControlTransportTest (EFI_PHYSICAL_ADDRESS, EFI_SMM_CONTROL2_PROTOCOL *);
EFI_STATUS EFIAPI VtpmWindowsTransportTest (EFI_PHYSICAL_ADDRESS, EFI_SMM_CONTROL2_PROTOCOL *);
EFI_STATUS EFIAPI VtpmWindowsPublish (EFI_PHYSICAL_ADDRESS, EFI_ACPI_TABLE_PROTOCOL *);
STATIC EFI_PHYSICAL_ADDRESS mSharedAddress;

STATIC
EFI_STATUS
VtpmInstallProbeTable (
  IN EFI_ACPI_TABLE_PROTOCOL *AcpiTable,
  OUT UINTN *TableKey
  )
{
  UINTN Index;
  UINTN Matches;
  UINTN PatchOffset;
  UINT64 Value;
  UINT8 Checksum;
  EFI_ACPI_DESCRIPTION_HEADER *Table;

  Matches = 0;
  PatchOffset = 0;
  for (Index = 0; Index + sizeof (Value) <= sizeof (vtpmruntimeprobe_aml_code); ++Index) {
    CopyMem (&Value, &vtpmruntimeprobe_aml_code[Index], sizeof (Value));
    if (Value == VTPM_PROBE_AML_ADDRESS_MARKER) {
      ++Matches;
      PatchOffset = Index;
    }
  }
  if (Matches != 1) {
    return EFI_COMPROMISED_DATA;
  }
  CopyMem (&vtpmruntimeprobe_aml_code[PatchOffset], &mSharedAddress, sizeof (mSharedAddress));
  Table = (EFI_ACPI_DESCRIPTION_HEADER *)vtpmruntimeprobe_aml_code;
  Table->Checksum = 0;
  Checksum = 0;
  for (Index = 0; Index < sizeof (vtpmruntimeprobe_aml_code); ++Index) {
    Checksum = (UINT8)(Checksum + vtpmruntimeprobe_aml_code[Index]);
  }
  Table->Checksum = (UINT8)(0 - Checksum);
  return AcpiTable->InstallAcpiTable (AcpiTable, Table, sizeof (vtpmruntimeprobe_aml_code), TableKey);
}

STATIC
VOID
EFIAPI
VtpmProbeReadyToBoot (
  IN EFI_EVENT Event,
  IN VOID *Context
  )
{
  EFI_STATUS Status;
  EFI_SMM_COMMUNICATION_PROTOCOL *Communication;
  EFI_SMM_COMMUNICATE_HEADER *Header;
  EFI_PHYSICAL_ADDRESS Address;
  VTPM_PROBE_MESSAGE Message;
  UINTN Size;
  UINT32 Sequence;
  EFI_SMM_CONTROL2_PROTOCOL *Control;
  UINT8 Command;
  UINT8 Data;
  VTPM_PROBE_PACKET *Packet;
  UINT32 Payload;
  UINT32 ExpectedResult;
  EFI_ACPI_TABLE_PROTOCOL *Acpi;

  gBS->CloseEvent (Event);
  Status = gBS->LocateProtocol (&gEfiSmmCommunicationProtocolGuid, NULL,
                               (VOID **)&Communication);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: SmmCommunication: %r\n", Status));
    return;
  }

  Address = 0;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiReservedMemoryType, 1, &Address);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: AllocatePages: %r\n", Status));
    return;
  }

  Header = (EFI_SMM_COMMUNICATE_HEADER *)(UINTN)Address;
  for (Sequence = 1; Sequence <= 2; ++Sequence) {
    ZeroMem (Header, EFI_PAGE_SIZE);
    CopyGuid (&Header->HeaderGuid, &mProbeGuid);
    Header->MessageLength = sizeof (Message);
    ZeroMem (&Message, sizeof (Message));
    Message.Signature = VTPM_PROBE_SIGNATURE;
    Message.Version   = VTPM_PROBE_VERSION;
    Message.Operation = VTPM_PROBE_PING;
    Message.Sequence  = Sequence;
    Message.Result    = MAX_UINT32;
    CopyMem (Header->Data, &Message, sizeof (Message));
    Size = OFFSET_OF (EFI_SMM_COMMUNICATE_HEADER, Data) + sizeof (Message);
    Status = Communication->Communicate (Communication, Header, &Size);
    CopyMem (&Message, Header->Data, sizeof (Message));
    if (EFI_ERROR (Status) ||
        Size != OFFSET_OF (EFI_SMM_COMMUNICATE_HEADER, Data) + sizeof (Message) ||
        Header->MessageLength != sizeof (Message) ||
        Message.Signature != VTPM_PROBE_SIGNATURE ||
        Message.Version != VTPM_PROBE_VERSION ||
        Message.Operation != VTPM_PROBE_PING ||
        Message.Sequence != Sequence || Message.Result != 0 ||
        Message.Calls != Sequence) {
      DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: sequence=%u status=%r result=%u calls=%u\n",
              Sequence, Status, Message.Result, Message.Calls));
      break;
    }
    DEBUG ((DEBUG_INFO, "[VTPM-DXE] PASS: sequence=%u calls=%u\n",
            Sequence, Message.Calls));
  }

  if (Sequence == 3) {
    DEBUG ((DEBUG_INFO, "[VTPM-DXE] PASS: DXE-SMM round trip verified (before ExitBootServices)\n"));
    ZeroMem (&Message, sizeof (Message));
    Message.Signature = VTPM_PROBE_SIGNATURE;
    Message.Version = VTPM_PROBE_VERSION;
    Message.Operation = VTPM_PROBE_REGISTER_BUFFER;
    Message.SharedAddress = mSharedAddress;
    Message.Result = MAX_UINT32;
    CopyMem (Header->Data, &Message, sizeof (Message));
    Size = OFFSET_OF (EFI_SMM_COMMUNICATE_HEADER, Data) + sizeof (Message);
    Status = Communication->Communicate (Communication, Header, &Size);
    CopyMem (&Message, Header->Data, sizeof (Message));
    if (EFI_ERROR (Status) || Message.Result != 0) {
      DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: buffer registration: %r result=%u\n",
              Status, Message.Result));
      gBS->FreePages (Address, 1);
      return;
    }
    Status = gBS->LocateProtocol (&gEfiSmmControl2ProtocolGuid, NULL,
                                 (VOID **)&Control);
    if (!EFI_ERROR (Status)) {
      Packet = (VTPM_PROBE_PACKET *)(UINTN)mSharedAddress;
      // Valid request, oversized request, short response capacity, unknown payload.
      for (Sequence = 1; Sequence <= 4; ++Sequence) {
        ZeroMem (Packet, sizeof (*Packet));
        Packet->Signature = VTPM_PROBE_SIGNATURE;
        Packet->Version = VTPM_PROBE_VERSION;
        Packet->Sequence = Sequence;
        Packet->RequestSize = Sequence == 2 ? VTPM_PACKET_CAPACITY + 1 : sizeof (Payload);
        Packet->ResponseCapacity = Sequence == 3 ? 1 : VTPM_PACKET_CAPACITY;
        Packet->Result = MAX_UINT32;
        Payload = Sequence == 4 ? 0 : VTPM_PACKET_PING;
        CopyMem (Packet->Request, &Payload, sizeof (Payload));
        Packet->State = VTPM_PACKET_READY;
        Command = VTPM_PROBE_SMI_COMMAND;
        Data = VTPM_PROBE_SMI_REQUEST;
        Status = Control->Trigger (Control, &Command, &Data, FALSE, 0);
        ExpectedResult = Sequence == 1 ? 0 : (Sequence == 4 ? 2 : 3);
        CopyMem (&Payload, Packet->Response, sizeof (Payload));
        if (EFI_ERROR (Status) || IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_PROBE_SMI_ACK ||
            Packet->State != VTPM_PACKET_DONE || Packet->ResponseSequence != Sequence ||
            Packet->Result != ExpectedResult ||
            Packet->ResponseSize != (Sequence == 1 ? sizeof (Payload) : 0) ||
            (Sequence == 1 && Payload != VTPM_PACKET_PONG)) {
          DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: buffer test=%u status=%r result=%u\n",
                  Sequence, Status, Packet->Result));
          break;
        }
        DEBUG ((DEBUG_INFO, "[VTPM-DXE] PASS: buffer test=%u result=%u\n",
                Sequence, Packet->Result));
      }
      if (Sequence == 5) {
        DEBUG ((DEBUG_INFO, "[VTPM-DXE] PASS: shared buffer and bounds checks verified\n"));
        {
          STATIC CONST UINT8 Cap[] = {0x80,1,0,0,0,22,0,0,1,0x7a,
                                     0,0,0,6,0,0,1,5,0,0,0,1};
          STATIC CONST UINT8 Rand[] = {0x80,1,0,0,0,12,0,0,1,0x7b,0,32};
          for (Sequence = 5; Sequence <= 6; ++Sequence) {
            ZeroMem (Packet, sizeof (*Packet));
            Packet->Signature = VTPM_PROBE_SIGNATURE;
            Packet->Version = VTPM_PROBE_VERSION;
            Packet->Sequence = Sequence;
            Packet->RequestSize = Sequence == 5 ? sizeof (Cap) : sizeof (Rand);
            Packet->ResponseCapacity = VTPM_PACKET_CAPACITY;
            CopyMem (Packet->Request, Sequence == 5 ? Cap : Rand, Packet->RequestSize);
            Packet->Result = MAX_UINT32;
            Packet->State = VTPM_PACKET_READY;
            Command = VTPM_PROBE_SMI_COMMAND;
            Data = VTPM_PROBE_SMI_REQUEST;
            Status = Control->Trigger (Control, &Command, &Data, FALSE, 0);
            if (EFI_ERROR (Status) || IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_PROBE_SMI_ACK ||
                Packet->State != VTPM_PACKET_DONE || Packet->ResponseSequence != Sequence ||
                Packet->Result != 0 || Packet->ResponseSize != (Sequence == 5 ? 27 : 44) ||
                Packet->Response[0] != 0x80 || Packet->Response[1] != 1 ||
                Packet->Response[2] != 0 || Packet->Response[3] != 0 ||
                Packet->Response[4] != 0 || Packet->Response[5] != Packet->ResponseSize ||
                Packet->Response[6] != 0 || Packet->Response[7] != 0 ||
                Packet->Response[8] != 0 || Packet->Response[9] != 0 ||
                (Sequence == 5 && CompareMem (&Packet->Response[23], "VTPM", 4) != 0) ||
                (Sequence == 6 && (Packet->Response[10] != 0 || Packet->Response[11] != 32))) {
              DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: TPM shared-buffer test=%u result=%u\n",
                      Sequence, Packet->Result));
              break;
            }
            DEBUG ((DEBUG_INFO, "[VTPM-DXE] PASS: TPM %a through shared buffer\n",
                    Sequence == 5 ? "GetCapability" : "GetRandom"));
          }
          if (Sequence == 7) {
            if (!EFI_ERROR (VtpmFullTransportTest (mSharedAddress, Control))) {
              if (!EFI_ERROR (VtpmCrbTransportTest (mSharedAddress, Control))) {
                if (!EFI_ERROR (VtpmControlTransportTest (mSharedAddress, Control)) &&
                    !EFI_ERROR (VtpmWindowsTransportTest (mSharedAddress, Control))) {
                  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&Acpi);
                  if (!EFI_ERROR (Status)) Status = VtpmWindowsPublish (mSharedAddress, Acpi);
                  if (EFI_ERROR (Status)) DEBUG ((DEBUG_ERROR, "[VTPM-WIN] FAIL: ACPI publication: %r\n", Status));
                }
              }
            }
          }
        }
      }
    } else {
      DEBUG ((DEBUG_ERROR, "[VTPM-DXE] FAIL: SmmControl2: %r\n", Status));
    }
  }
  gBS->FreePages (Address, 1);
}

EFI_STATUS
EFIAPI
VtpmProbeDxeEntryPoint (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_EVENT Event;
  EFI_STATUS Status;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTable;
  UINTN TableKey;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL,
                               (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  // Windows ACPI Control Area must be AddressRangeReserved and _CRS uses
  // Memory32Fixed. Keep the complete registered allocation reserved below 4 GiB.
  mSharedAddress = 0xFFFFFFFF;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiReservedMemoryType, VTPM_SHARED_PAGES, &mSharedAddress);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  ZeroMem ((VOID *)(UINTN)mSharedAddress, VTPM_SHARED_SIZE);
  Status = VtpmInstallProbeTable (AcpiTable, &TableKey);
  DEBUG ((DEBUG_INFO, "[VTPM-DXE] Runtime probe SSDT installation: %r\n", Status));
  if (EFI_ERROR (Status)) {
    gBS->FreePages (mSharedAddress, VTPM_SHARED_PAGES);
    return Status;
  }

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                              VtpmProbeReadyToBoot, NULL,
                              &gEfiEventReadyToBootGuid, &Event);
  DEBUG ((DEBUG_INFO, "[VTPM-DXE] ReadyToBoot probe registration: %r\n", Status));
  if (EFI_ERROR (Status)) {
    AcpiTable->UninstallAcpiTable (AcpiTable, TableKey);
    gBS->FreePages (mSharedAddress, VTPM_SHARED_PAGES);
  }
  return Status;
}
