#include <Library/BaseMemoryLib.h>

VOID *VtpmCopyMem (VOID *Destination, CONST VOID *Source, UINTN Size)
{
    return CopyMem(Destination, Source, Size);
}
