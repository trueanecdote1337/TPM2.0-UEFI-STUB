#ifndef VTPM_CORE_SMM_H_
#define VTPM_CORE_SMM_H_
#include <Uefi.h>
extern EFI_GUID gVtpmCoreSmmProtocolGuid;
#define VTPM_CORE_SMM_REVISION 2
// SMM-private protocol. Callers supply validated private buffers, never OS pointers.
typedef UINT32 (EFIAPI *VTPM_CORE_SUBMIT)(UINT32, CONST UINT8 *, UINT32, UINT8 *, UINT32 *);
typedef struct {
  UINT32 Revision;
  UINT32 ResponseCapacity;
  UINT32 CommandCapacity;
  VTPM_CORE_SUBMIT Submit;
} VTPM_CORE_SMM_PROTOCOL;
#endif
