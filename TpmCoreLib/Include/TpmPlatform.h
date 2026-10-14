#ifndef VTPM_PLATFORM_H_
#define VTPM_PLATFORM_H_
#include <Base.h>
// Trusted core lifecycle only: resets clock and signals, does not manufacture NV.
VOID EFIAPI VtpmPlatformPowerOn(VOID);
#endif
