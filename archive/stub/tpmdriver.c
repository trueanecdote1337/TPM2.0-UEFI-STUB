#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/AcpiTable.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Tpm20.h>
#include <IndustryStandard/Tpm2Acpi.h>
#include "SsdtVtpm.h"


  #ifndef EFI_TPM2_ACPI_TABLE_SIGNATURE
#define EFI_TPM2_ACPI_TABLE_SIGNATURE  SIGNATURE_32 ('T', 'P', 'M', '2')
#endif

#define TPM_CRB_BASE_ADDRESS  0xFED40000ULL

#pragma pack(1)
typedef struct {
  UINT32  LocState;          // 0x00
  UINT32  Reserved1;         // 0x04
  UINT32  LocCtrl;           // 0x08
  UINT32  LocSts;            // 0x0C
  UINT8   Reserved2[32];     // 0x10 - 0x2F
  UINT32  IntfId;            // 0x30
  UINT32  IntfIdH;           // 0x34
  UINT32  CtrlExt;           // 0x38
  UINT32  Reserved3;         // 0x3C
  
  // Control Area (0x40)
  UINT32  Request;           // 0x40
  UINT32  Status;            // 0x44: Control Status
  UINT32  Cancel;            // 0x48
  UINT32  Start;             // 0x4C: Start Execution
  UINT64  InterruptEnable;   // 0x50
  UINT32  InterruptStatus;   // 0x58
  UINT32  CommandSize;       // 0x5C
  UINT64  CommandAddress;    // 0x60
  UINT32  ResponseSize;      // 0x68
  UINT64  ResponseAddress;   // 0x6C
} TPM_CRB_REGISTERS;

typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER  Header;
  UINT16                       Flags;
  UINT16                       Reserved;
  UINT64                       AddressOfControlArea;
  UINT32                       StartMethod;
} EFI_TPM2_ACPI_TABLE_FIXED;
#pragma pack()

// Минимальный фейковый заголовок ответа TPM 2.0 (TPM_RC_SUCCESS)
// Tag: TPM_ST_NO_SESSIONS (0x8001), Size: 10 bytes, ResponseCode: 0x00000000
STATIC UINT8 mFakeTpmResponse[10] = {
  0x80, 0x01,             // ST_NO_SESSIONS
  0x00, 0x00, 0x00, 0x0A, // Size = 10
  0x00, 0x00, 0x00, 0x00  // TPM_RC_SUCCESS
};

EFI_STATUS
EFIAPI
VtpmDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                  Status;
  EFI_ACPI_TABLE_PROTOCOL     *AcpiTable;
  UINTN                       TableKey;
  EFI_ACPI_DESCRIPTION_HEADER *SsdtHeader;
  EFI_TPM2_ACPI_TABLE_FIXED   Tpm2Table;
  EFI_PHYSICAL_ADDRESS        TpmBase;
  TPM_CRB_REGISTERS           *CrbRegs;
  UINT8                       *ResponseBuffer;

  DEBUG ((DEBUG_INFO, "[VTPM] Initializing TPM 2.0 Active Stub Driver...\n"));

  // 1. Выделяем память под регистры и буферы (8 страниц = 32 КБ)
  TpmBase = TPM_CRB_BASE_ADDRESS;
  Status = gBS->AllocatePages (
                  AllocateAddress,
                  EfiRuntimeServicesData,
                  8,
                  &TpmBase
                  );

  CrbRegs = (TPM_CRB_REGISTERS *)(UINTN)TPM_CRB_BASE_ADDRESS;
  ZeroMem (CrbRegs, 0x2000); // Очищаем регистры и область команд

  // 2. Инициализируем базовые значения регистров CRB
  CrbRegs->IntfId   = 0x00000001; // CRB Active
  CrbRegs->LocState = 0x00000082; // Locality Assigned & Valid
  CrbRegs->Status   = 0x00000000; // Ready / No Error
  CrbRegs->Start    = 0x00000000; // Start = 0 (Не запущен / прошлый запуск завершен)

  // Настройка буферов команд/ответов
  CrbRegs->CommandSize     = 0x1000;
  CrbRegs->CommandAddress  = TPM_CRB_BASE_ADDRESS + 0x1000;
  CrbRegs->ResponseSize    = 0x1000;
  CrbRegs->ResponseAddress = TPM_CRB_BASE_ADDRESS + 0x1000; // Совмещенный буфер для упрощения

  // Помещаем успешный фейковый ответ в буфер
  ResponseBuffer = (UINT8 *)(UINTN)(TPM_CRB_BASE_ADDRESS + 0x1000);
  CopyMem (ResponseBuffer, mFakeTpmResponse, sizeof (mFakeTpmResponse));

  // 3. Установка ACPI SSDT
  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SsdtHeader = (EFI_ACPI_DESCRIPTION_HEADER *)ssdtvtpm_aml_code;
  Status = AcpiTable->InstallAcpiTable (AcpiTable, SsdtHeader, SsdtHeader->Length, &TableKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // 4. Установка ACPI TPM2 таблицы
  ZeroMem (&Tpm2Table, sizeof (EFI_TPM2_ACPI_TABLE_FIXED));
  Tpm2Table.Header.Signature       = SIGNATURE_32 ('T', 'P', 'M', '2');
  Tpm2Table.Header.Length          = sizeof (EFI_TPM2_ACPI_TABLE_FIXED);
  Tpm2Table.Header.Revision        = 4;
  CopyMem (Tpm2Table.Header.OemId, "VTPM2 ", 6);
  Tpm2Table.Header.OemTableId      = SIGNATURE_64 ('V', 'T', 'P', 'M', 'E', 'N', 'U', 'M');
  Tpm2Table.Header.OemRevision     = 0x00000001;
  Tpm2Table.Header.CreatorId       = SIGNATURE_32 ('E', 'D', 'K', '2');
  Tpm2Table.Header.CreatorRevision = 0x00000001;

  Tpm2Table.Flags                  = 0; // MMIO
  Tpm2Table.AddressOfControlArea   = TPM_CRB_BASE_ADDRESS + 0x0040;
  Tpm2Table.StartMethod            = 7; // CRB

  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        &Tpm2Table,
                        Tpm2Table.Header.Length,
                        &TableKey
                        );

  return Status;
}