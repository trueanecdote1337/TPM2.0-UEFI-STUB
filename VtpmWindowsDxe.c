#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiTable.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include "VtpmControl.h"
#include "VtpmWindows.hex"
// Inspect firmware ACPI roots before installing; never create a second TPM2.
STATIC BOOLEAN HasTpm2(VOID) {
  UINTN I, J, Width; UINT64 Address;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER *Root, *Child;
  for(I=0;I<gST->NumberOfTableEntries;++I) {
    if(!CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiAcpi20TableGuid) &&
       !CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiAcpi10TableGuid)) continue;
    Rsdp=gST->ConfigurationTable[I].VendorTable;
    Width=4; Address=Rsdp->RsdtAddress;
    if(Rsdp->Revision>=2 && Rsdp->XsdtAddress!=0) { Width=8; Address=Rsdp->XsdtAddress; }
    if(Address==0) continue;
    Root=(VOID *)(UINTN)Address;
    if(Root->Length<sizeof(*Root) || Root->Length>0x100000) return TRUE;
    for(J=sizeof(*Root);J+Width<=Root->Length;J+=Width) {
      Address=0; CopyMem(&Address,(UINT8 *)Root+J,Width);
      if(Address==0) continue;
      Child=(VOID *)(UINTN)Address;
      if(Child->Signature==SIGNATURE_32('T','P','M','2')) return TRUE;
    }
  }
  return FALSE;
}
STATIC VOID Checksum(EFI_ACPI_DESCRIPTION_HEADER *Table) {
  UINTN I; UINT8 Sum=0;
  Table->Checksum=0;
  for(I=0;I<Table->Length;++I) Sum=(UINT8)(Sum+((UINT8 *)Table)[I]);
  Table->Checksum=(UINT8)(0-Sum);
}
EFI_STATUS EFIAPI VtpmWindowsPublish(EFI_PHYSICAL_ADDRESS Base, EFI_ACPI_TABLE_PROTOCOL *Acpi) {
  EFI_TPM2_ACPI_TABLE T;
  UINT64 Value, Address=Base+VTPM_CONTROL_OFFSET;
  UINTN I, Found=0, Offset=0, SsdtKey, TpmKey;
  EFI_STATUS Status;
  if(HasTpm2()) {
    DEBUG((DEBUG_ERROR,"[VTPM-WIN] Existing TPM2 table; refusing duplicate publication\n"));
    return EFI_ALREADY_STARTED;
  }
  for(I=0;I+sizeof(Value)<=sizeof(vtpmwindows_aml_code);++I) {
    CopyMem(&Value,vtpmwindows_aml_code+I,sizeof(Value));
    if(Value==VTPM_PROBE_AML_ADDRESS_MARKER) { ++Found; Offset=I; }
  }
  if(Found!=1 || Address+0x3000>0x100000000ULL) return EFI_COMPROMISED_DATA;
  CopyMem(vtpmwindows_aml_code+Offset,&Address,sizeof(Address));
  Checksum((EFI_ACPI_DESCRIPTION_HEADER *)vtpmwindows_aml_code);
  ZeroMem(&T,sizeof(T));
  T.Header.Signature=SIGNATURE_32('T','P','M','2'); T.Header.Length=sizeof(T);
  // Revision 3: plain ACPI Control Area, no revision-4 log fields or CRB params.
  T.Header.Revision=3; CopyMem(T.Header.OemId,"VTPM2 ",6);
  CopyMem(&T.Header.OemTableId,"VTPMWIN ",8);
  T.Header.OemRevision=1; T.Header.CreatorId=SIGNATURE_32('E','D','K','2');
  T.Header.CreatorRevision=1;
  T.AddressOfControlArea=Address; T.StartMethod=EFI_TPM2_ACPI_TABLE_START_METHOD_ACPI;
  Checksum(&T.Header);
  Status=Acpi->InstallAcpiTable(Acpi,vtpmwindows_aml_code,sizeof(vtpmwindows_aml_code),&SsdtKey);
  if(EFI_ERROR(Status)) return Status;
  Status=Acpi->InstallAcpiTable(Acpi,&T,sizeof(T),&TpmKey);
  if(EFI_ERROR(Status)) { Acpi->UninstallAcpiTable(Acpi,SsdtKey); return Status; }
  DEBUG((DEBUG_INFO,"[VTPM-WIN] PASS: TPM2/MSFT0101 published; StartMethod=2 control=0x%Lx\n",Address));
  return EFI_SUCCESS;
}
