// EDK II adaptation of Platform/src/NVMem.c's interface.
// Volatile bring-up backend: storage belongs to the SMM module, never ACPI NVS.
#include "Tpm.h"
#include <TpmCoreProbe.h>

static BYTE mNv[NV_MEMORY_SIZE];
static BOOL mInitialized;
static BOOL mAvailable;

// Subtraction prevents an offset + size wrap from passing the range check.
static BOOL NvRange(unsigned int offset, unsigned int size)
{
    return offset <= NV_MEMORY_SIZE && size <= NV_MEMORY_SIZE - offset;
}

static BOOL NvBuffer(unsigned int offset, unsigned int size, const void *data)
{
    return NvRange(offset, size) && (size == 0 || data != NULL);
}

static void NvRequire(BOOL valid)
{
    if(!valid)
        FAIL(FATAL_ERROR_INTERNAL);
}

int _plat__NVEnable(void *parameter, size_t parameterSize)
{
    // This backend supports only the default configuration; no external image.
    if(parameter != NULL || parameterSize != 0)
        return -1;
    if(!mInitialized)
    {
        SetMem(mNv, sizeof(mNv), 0xff);
        mInitialized = TRUE;
    }
    mAvailable = TRUE;
    return 0;
}

// NVDisable is a host/platform lifecycle API, not a TPM command.
void _plat__NVDisable(void *parameter, size_t parameterSize)
{
    (void)parameterSize;
    mAvailable = FALSE;
    if(parameter != NULL)
        _plat__TearDown();
}

int _plat__GetNvReadyState(void)
{
    return mAvailable ? NV_READY : NV_WRITEFAILURE;
}

int _plat__NvMemoryRead(unsigned int offset, unsigned int size, void *data)
{
    NvRequire(mInitialized && NvBuffer(offset, size, data));
    if(size != 0)
        CopyMem(data, &mNv[offset], size);
    return TRUE;
}

int _plat__NvGetChangedStatus(unsigned int offset, unsigned int size, void *data)
{
    NvRequire(mInitialized && NvBuffer(offset, size, data));
    return size != 0 && CompareMem(&mNv[offset], data, size) != 0;
}

int _plat__NvMemoryWrite(unsigned int offset, unsigned int size, void *data)
{
    NvRequire(mInitialized && NvBuffer(offset, size, data));
    if(size != 0)
        CopyMem(&mNv[offset], data, size);
    return TRUE;
}

int _plat__NvMemoryClear(unsigned int offset, unsigned int size)
{
    NvRequire(mInitialized && NvRange(offset, size));
    if(size != 0)
        SetMem(&mNv[offset], size, 0xff);
    return TRUE;
}

int _plat__NvMemoryMove(unsigned int source, unsigned int destination,
                       unsigned int size)
{
    NvRequire(mInitialized && NvRange(source, size) && NvRange(destination, size));
    // EDK II CopyMem explicitly supports overlapping buffers.
    if(size != 0)
        CopyMem(&mNv[destination], &mNv[source], size);
    return TRUE;
}

int _plat__NvCommit(void)
{
    // RAM already contains the complete image. This is NOT persistent storage.
    return mAvailable ? 0 : 1;
}

void _plat__TearDown(void)
{
    // Volatile writes prevent dead-store elimination of secret erasure.
    volatile BYTE *cursor = mNv;
    UINTN size = sizeof(mNv);
    while(size-- != 0)
        *cursor++ = 0;
    mInitialized = FALSE;
    mAvailable = FALSE;
}

UINT32 EFIAPI VtpmNvSmokeTest(void)
{
    UINT32 passed = 0;
    BYTE data[8];
    BYTE pattern[8] = {0,1,2,3,4,5,6,7};
    BYTE forward[8] = {0,1,0,1,2,3,4,5};
    BYTE backward[8] = {2,3,4,5,6,7,6,7};
    BYTE erased[8];
    SetMem(erased, sizeof(erased), 0xff);
    _plat__TearDown();
    if(_plat__GetNvReadyState() == NV_WRITEFAILURE &&
       _plat__NvCommit() != 0 && _plat__NVEnable(NULL, 0) == 0)
    {
        _plat__NvMemoryRead(NV_MEMORY_SIZE - sizeof(data), sizeof(data), data);
        if(CompareMem(data, erased, sizeof(data)) == 0 &&
           _plat__GetNvReadyState() == NV_READY)
            passed |= VTPM_NV_TEST_ERASE;
    }
    _plat__NvMemoryWrite(NV_MEMORY_SIZE - sizeof(pattern), sizeof(pattern), pattern);
    _plat__NvMemoryRead(NV_MEMORY_SIZE - sizeof(data), sizeof(data), data);
    if(CompareMem(data, pattern, sizeof(data)) == 0 &&
       _plat__NvGetChangedStatus(NV_MEMORY_SIZE - sizeof(data), sizeof(data), pattern) == NV_IS_SAME &&
       _plat__NvGetChangedStatus(NV_MEMORY_SIZE - sizeof(data), sizeof(data), erased) == NV_HAS_CHANGED)
        passed |= VTPM_NV_TEST_READ_WRITE;

    _plat__NvMemoryWrite(0, sizeof(pattern), pattern);
    _plat__NvMemoryMove(0, 2, 6);
    _plat__NvMemoryRead(0, sizeof(data), data);
    if(CompareMem(data, forward, sizeof(data)) == 0)
    {
        _plat__NvMemoryWrite(0, sizeof(pattern), pattern);
        _plat__NvMemoryMove(2, 0, 6);
        _plat__NvMemoryRead(0, sizeof(data), data);
        if(CompareMem(data, backward, sizeof(data)) == 0)
            passed |= VTPM_NV_TEST_OVERLAP;
    }

    // Exercise exactly the validators used before every access. Deliberately
    // avoid calling NvRequire on invalid data: that poisons the TPM failure state.
    if(NvBuffer(NV_MEMORY_SIZE, 0, NULL) &&
       !NvBuffer(NV_MEMORY_SIZE, 1, data) &&
       !NvRange(NV_MEMORY_SIZE + 1, 0) &&
       !NvRange(UINT32_MAX, 2) && !NvRange(1, UINT32_MAX) &&
       !NvBuffer(0, 1, NULL))
    {
        _plat__NvMemoryRead(NV_MEMORY_SIZE, 0, NULL);
        _plat__NvMemoryWrite(NV_MEMORY_SIZE, 0, NULL);
        _plat__NvMemoryMove(NV_MEMORY_SIZE, NV_MEMORY_SIZE, 0);
        passed |= VTPM_NV_TEST_BOUNDS;
    }

    _plat__NVDisable(NULL, 0);
    if(_plat__NvCommit() != 0 && _plat__NVEnable(NULL, 0) == 0)
    {
        _plat__NvMemoryRead(NV_MEMORY_SIZE - sizeof(data), sizeof(data), data);
        if(CompareMem(data, pattern, sizeof(data)) == 0 && _plat__NvCommit() == 0)
        {
            _plat__NvMemoryClear(NV_MEMORY_SIZE - sizeof(data), sizeof(data));
            _plat__NvMemoryRead(NV_MEMORY_SIZE - sizeof(data), sizeof(data), data);
            if(CompareMem(data, erased, sizeof(data)) == 0)
            {
                _plat__TearDown();
                if(_plat__NVEnable(NULL, 0) == 0)
                {
                    _plat__NvMemoryRead(0, sizeof(data), data);
                    if(CompareMem(data, erased, sizeof(data)) == 0)
                        passed |= VTPM_NV_TEST_LIFECYCLE;
                }
            }
        }
    }
    // This destructive probe must run before manufacture, never on live keys.
    _plat__TearDown();
    return passed;
}
