// EDK II bring-up adaptation of VendorInfo, ExtraData, PowerPlat,
// LocalityPlat, Cancel and PPPlat platform interfaces.
#include "Tpm.h"
#include <TpmCoreProbe.h>
#include <TpmPlatform.h>

void _plat__TimerReset(void);
static BOOL mPowerLost = TRUE;
static BYTE mLocality;
static volatile BOOL mCanceled;

uint32_t _plat__GetManufacturerCapabilityCode(void)
{
    return 0x5654504d; // "VTPM": project identifier, not a registered TCG vendor.
}
uint32_t _plat__GetVendorCapabilityCode(int index)
{
    switch(index)
    {
        case 1: return 0x45444b32; // "EDK2"
        case 2: return 0x7654504d; // "vTPM"
        case 3: return 0x454d5520; // "EMU "
        default: return 0;
    }
}
uint32_t _plat__GetTpmFirmwareVersionHigh(void) { return 0x00000001; }
uint32_t _plat__GetTpmFirmwareVersionLow(void) { return 0x00000000; }
uint16_t _plat__GetTpmFirmwareSvn(void) { return 0; }
uint16_t _plat__GetTpmFirmwareMaxSvn(void) { return 0; }
uint32_t _plat__GetTpmType(void) { return 1; }

void _plat__GetPlatformManufactureData(uint8_t *data, uint32_t size)
{
    if(size != 0)
    {
        if(data == NULL)
            FAIL(FATAL_ERROR_INTERNAL);
        // No platform-specific secrets/identity in this optional data block.
        SetMem(data, size, 0xff);
    }
}

int _plat__WasPowerLost(void)
{
    int value = mPowerLost;
    mPowerLost = FALSE;
    return value;
}
unsigned char _plat__LocalityGet(void) { return mLocality; }
void _plat__LocalitySet(unsigned char locality)
{
    // Retain the reference platform's normalization of reserved localities.
    mLocality = locality > 4 && locality < 32 ? 0 : locality;
}
int _plat__IsCanceled(void) { return mCanceled; }
void _plat__SetCancel(void) { mCanceled = TRUE; }
void _plat__ClearCancel(void) { mCanceled = FALSE; }
int _plat__PhysicalPresenceAsserted(void) { return FALSE; }

VOID EFIAPI VtpmPlatformPowerOn(VOID)
{
    _plat__TimerReset();
    mPowerLost = TRUE;
    mLocality = 0;
    mCanceled = FALSE;
    // NVEnable, manufacture and _TPM_Init belong to the core lifecycle wrapper.
}

UINT32 EFIAPI VtpmPlatformSmokeTest(VOID)
{
    UINT32 passed = 0;
    PCR_Attributes attributes;
    BYTE data[66];
    UINT16 length;
    UINTN index;
    BOOL valid;
    if(_plat__GetManufacturerCapabilityCode() == 0x5654504d &&
       _plat__GetVendorCapabilityCode(1) == 0x45444b32 &&
       _plat__GetVendorCapabilityCode(0) == 0 &&
       _plat__GetVendorCapabilityCode(5) == 0 &&
       _plat__GetTpmFirmwareVersionHigh() != 0)
        passed |= VTPM_PLATFORM_TEST_INFO;
    attributes = _platPcr__GetPcrInitializationAttributes(0);
    if(_platPcr__NumberOfPcrs() == 24 && attributes.stateSave &&
       attributes.resetLocality == 0 && attributes.extendLocality == 0x1f &&
       _platPcr_IsPcrBankDefaultActive(TPM_ALG_SHA256))
    {
        attributes = _platPcr__GetPcrInitializationAttributes(17);
        if(!attributes.stateSave && attributes.resetLocality == 0x10 &&
           attributes.extendLocality == 0x1c)
            passed |= VTPM_PLATFORM_TEST_PCR_PROFILE;
    }
    SetMem(data, sizeof(data), 0xa5);
    length = 0;
    valid = _platPcr__GetInitialValueForPcr(0, TPM_ALG_SHA256, 3,
                &data[1], 32, &length) == TPM_RC_SUCCESS && length == 32 &&
                data[0] == 0xa5 && data[33] == 0xa5 && data[32] == 3;
    for(index = 1; index < 32; ++index)
        if(data[index] != 0) valid = FALSE;
    if(valid)
    {
        valid = _platPcr__GetInitialValueForPcr(17, TPM_ALG_SHA256, 0,
                    &data[1], 32, &length) == TPM_RC_SUCCESS && length == 32;
        for(index = 1; index <= 32; ++index)
            if(data[index] != 0xff) valid = FALSE;
        if(valid) passed |= VTPM_PLATFORM_TEST_PCR_VALUES;
    }
    SetMem(data, sizeof(data), 0xa5);
    length = 99;
    valid = _platPcr__GetInitialValueForPcr(0, TPM_ALG_NULL, 0,
                    &data[1], 32, &length) == TPM_RC_PCR && length == 0 &&
            _platPcr__GetInitialValueForPcr(24, TPM_ALG_SHA256, 0,
                    &data[1], 32, &length) == TPM_RC_PCR &&
            _platPcr__GetInitialValueForPcr(0, TPM_ALG_SHA256, 0,
                    &data[1], 31, &length) == TPM_RC_FAILURE &&
            _platPcr__GetInitialValueForPcr(0, TPM_ALG_SHA256, 0,
                    NULL, 32, &length) == TPM_RC_FAILURE &&
            _platPcr__GetInitialValueForPcr(0, TPM_ALG_SHA256, 0,
                    &data[1], 32, NULL) == TPM_RC_FAILURE;
    for(index = 0; index < sizeof(data); ++index)
        if(data[index] != 0xa5) valid = FALSE;
    if(valid) passed |= VTPM_PLATFORM_TEST_PCR_BOUNDS;

    VtpmPlatformPowerOn();
    valid = _plat__WasPowerLost() && !_plat__WasPowerLost() &&
            _plat__LocalityGet() == 0 && !_plat__IsCanceled() &&
            !_plat__PhysicalPresenceAsserted();
    _plat__LocalitySet(4);
    valid = valid && _plat__LocalityGet() == 4;
    _plat__LocalitySet(31);
    valid = valid && _plat__LocalityGet() == 0;
    _plat__LocalitySet(32);
    valid = valid && _plat__LocalityGet() == 32;
    _plat__SetCancel();
    valid = valid && _plat__IsCanceled();
    _plat__ClearCancel();
    valid = valid && !_plat__IsCanceled();
    if(valid) passed |= VTPM_PLATFORM_TEST_SIGNALS;
    // Leave a fresh power-on indication for future _TPM_Init, not probe state.
    VtpmPlatformPowerOn();
    return passed;
}
