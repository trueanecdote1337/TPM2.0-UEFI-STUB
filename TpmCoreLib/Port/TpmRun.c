// EDK II execution/failure boundary for one TPM core owned by one SMM module.
#include "Tpm.h"
#include <Library/SynchronizationLib.h>
#include "ExecCommand_fp.h"
#include "_TPM_Init_fp.h"
#include <TpmPlatform.h>
#include <TpmCoreRun.h>

static BASE_LIBRARY_JUMP_BUFFER mJump;
static BASE_LIBRARY_JUMP_BUFFER *mBoundary;
static BOOL mAttempted;
static BOOL mReady;
static UINT32 mInitResult = VTPM_RUN_NOT_READY;
static volatile UINT32 mBusy;
static BYTE mRequest[MAX_COMMAND_SIZE];
static BYTE mResponse[MAX_RESPONSE_SIZE];
static volatile UINT32 mFailureCount;

UINT32 EFIAPI VtpmCoreCommandCapacity(void) { return MAX_COMMAND_SIZE; }
UINT32 EFIAPI VtpmCoreResponseCapacity(void) { return MAX_RESPONSE_SIZE; }

VOID EFIAPI VtpmCoreProbeBoundary(BASE_LIBRARY_JUMP_BUFFER *boundary)
{
    mBoundary = boundary;
}

NORETURN void _plat__Fail(void)
{
    if(mBoundary != NULL)
        LongJump(mBoundary, 1);
    // No uninitialized jump target. Reachable core calls must be protected.
    for(;;) CpuPause();
}

UINT32 EFIAPI VtpmCoreInitialize(void)
{
    BASE_LIBRARY_JUMP_BUFFER *previous = mBoundary;
    if(mAttempted)
        return mInitResult;
    mAttempted = TRUE;
    mInitResult = VTPM_RUN_FAILURE;
    mBoundary = &mJump;
    if(SetJump(&mJump) == 0)
    {
        VtpmPlatformPowerOn();
        if(_plat__NVEnable(NULL, 0) != 0)
            mInitResult = VTPM_RUN_NV;
        else if(TPM_Manufacture(MANUF_FIRST_TIME) != MANUF_OK)
            mInitResult = VTPM_RUN_MANUFACTURE;
        else
        {
            _TPM_Init();
            if(!g_inFailureMode)
            {
                mReady = TRUE;
                mInitResult = VTPM_RUN_OK;
            }
        }
    }
    mBoundary = previous;
    return mInitResult;
}

UINT32 EFIAPI VtpmCoreSubmit(UINT32 size, CONST UINT8 *request,
                            UINT32 capacity, UINT8 *response, UINT32 *responseSize)
{
    BASE_LIBRARY_JUMP_BUFFER *previous;
    BYTE *output;
    UINT32 outputSize;
    UINT32 result = VTPM_RUN_FAILURE;
    if(responseSize == NULL)
        return VTPM_RUN_ARGUMENT;
    *responseSize = 0;
    if(request == NULL || response == NULL || size > sizeof(mRequest) ||
       capacity < sizeof(mResponse))
        return VTPM_RUN_ARGUMENT;
    if(!mReady)
        return VTPM_RUN_NOT_READY;
    if(InterlockedCompareExchange32(&mBusy, 0, 1) != 0)
        return VTPM_RUN_NOT_READY;
    CopyMem(mRequest, request, size);
    previous = mBoundary;
    mBoundary = &mJump;
    mFailureCount = 0;
    if(SetJump(&mJump) != 0)
        ++mFailureCount;
    // One retry lets ExecuteCommand generate the upstream failure-mode response.
    // A failure in failure-response handling must not loop indefinitely.
    if(mFailureCount <= 1)
    {
        output = mResponse;
        outputSize = sizeof(mResponse);
        ExecuteCommand(size, mRequest, &outputSize, &output);
        if(output != NULL && outputSize >= 10 && outputSize <= sizeof(mResponse))
        {
            CopyMem(response, output, outputSize);
            *responseSize = outputSize;
            result = VTPM_RUN_OK;
        }
    }
    if(result != VTPM_RUN_OK)
        mReady = FALSE;
    mBoundary = previous;
    // Wipe transient command/response buffers; core state remains in SMRAM.
    SetMem(mRequest, sizeof(mRequest), 0);
    SetMem(mResponse, sizeof(mResponse), 0);
    InterlockedCompareExchange32(&mBusy, 1, 0);
    return result;
}

static BOOL Command(const BYTE *request, UINT32 size, BYTE *response,
                    UINT32 *responseSize, TPM_RC expected)
{
    return VtpmCoreSubmit(size, request, MAX_RESPONSE_SIZE, response, responseSize) == VTPM_RUN_OK &&
           BYTE_ARRAY_TO_UINT16(response) == TPM_ST_NO_SESSIONS &&
           BYTE_ARRAY_TO_UINT32(response + 2) == *responseSize &&
           BYTE_ARRAY_TO_UINT32(response + 6) == expected;
}

UINT32 EFIAPI VtpmCommandSmokeTest(UINT32 *stage)
{
    // Wire commands: TPM_ST_NO_SESSIONS, size, commandCode, parameters (big-endian).
    static const BYTE startup[] = {0x80,1,0,0,0,12,0,0,1,0x44,0,0};
    static const BYTE capability[] = {0x80,1,0,0,0,22,0,0,1,0x7a,
                                    0,0,0,6,0,0,1,5,0,0,0,1};
    static const BYTE selftest[] = {0x80,1,0,0,0,11,0,0,1,0x43,1};
    static const BYTE random[] = {0x80,1,0,0,0,12,0,0,1,0x7b,0,32};
    static const BYTE unknown[] = {0x80,1,0,0,0,10,0xff,0xff,0xff,0xff};
    static const BYTE disabledEcc[] = {0x80,1,0,0,0,10,0,0,1,0x78};
    static const BYTE badSize[] = {0x80,1,0,0,0,11,0,0,1,0x7b,0,32};
    BYTE response[MAX_RESPONSE_SIZE];
    UINT32 size;
    UINT32 passed = 0;
    BOOL guard;
    *stage = VtpmCoreInitialize();
    if(*stage != VTPM_RUN_OK)
        return 0;
    passed |= VTPM_COMMAND_TEST_INIT;
    SetMem(response, sizeof(response), 0xa5);
    guard = VtpmCoreSubmit(sizeof(startup), startup, MAX_RESPONSE_SIZE - 1,
                          response, &size) == VTPM_RUN_ARGUMENT && size == 0 &&
            response[0] == 0xa5 && response[MAX_RESPONSE_SIZE - 1] == 0xa5;
    if(Command(startup, sizeof(startup), response, &size, TPM_RC_SUCCESS) && size == 10)
        passed |= VTPM_COMMAND_TEST_STARTUP;
    if(Command(capability, sizeof(capability), response, &size, TPM_RC_SUCCESS) &&
       size == 27 && BYTE_ARRAY_TO_UINT32(response + 11) == TPM_CAP_TPM_PROPERTIES &&
       BYTE_ARRAY_TO_UINT32(response + 15) == 1 &&
       BYTE_ARRAY_TO_UINT32(response + 19) == TPM_PT_MANUFACTURER &&
       BYTE_ARRAY_TO_UINT32(response + 23) == 0x5654504d)
        passed |= VTPM_COMMAND_TEST_CAPABILITY;
    if(Command(selftest, sizeof(selftest), response, &size, TPM_RC_SUCCESS) && size == 10)
        passed |= VTPM_COMMAND_TEST_SELFTEST;
    if(Command(random, sizeof(random), response, &size, TPM_RC_SUCCESS) &&
       size == 44 && BYTE_ARRAY_TO_UINT16(response + 10) == 32)
        passed |= VTPM_COMMAND_TEST_RANDOM;
    if(guard && VtpmCoreInitialize() == VTPM_RUN_OK &&
       Command(disabledEcc, sizeof(disabledEcc), response, &size, TPM_RC_COMMAND_CODE) &&
       Command(unknown, sizeof(unknown), response, &size, TPM_RC_COMMAND_CODE) &&
       size == 10 && Command(badSize, sizeof(badSize), response, &size, TPM_RC_COMMAND_SIZE) &&
       size == 10 && Command(startup, sizeof(startup), response, &size, TPM_RC_INITIALIZE) &&
       Command(random, sizeof(random), response, &size, TPM_RC_SUCCESS))
        passed |= VTPM_COMMAND_TEST_ERRORS;
    // Do not expose random output in logs.
    SetMem(response, sizeof(response), 0);
    return passed;
}
