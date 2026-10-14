// x86 bring-up clock: calibrate a long-lived TSC against the OVMF ACPI timer.
#include "Tpm.h"
#include <Library/TimerLib.h>
#include <TpmCoreProbe.h>

#define CLOCK_NOMINAL 30000u
#define CLOCK_MIN 25000u
#define CLOCK_MAX 35000u
static UINT64 mFrequency;
static UINT64 mEpoch;
static UINT64 mLastCounter;
static UINT64 mLastUs;
static UINT64 mTpmUs;
static UINT32 mFraction;
static UINT32 mRate = CLOCK_NOMINAL;
static BOOL mReset;
static BOOL mStopped;

static UINT64 CounterDelta(UINT64 previous, UINT64 current, UINT64 end)
{
    return current >= previous ? current - previous : end - previous + current + 1;
}

static BOOL ClockInitialize(void)
{
    UINT64 start, end, pmBefore, pmAfter, tscBefore, tscAfter, ns, ticks;
    if(mFrequency != 0)
        return TRUE;
    if(GetPerformanceCounterProperties(&start, &end) == 0 || start != 0 ||
       end == MAX_UINT64)
        return FALSE; // This calibration path expects the short ascending ACPI timer.
    pmBefore = GetPerformanceCounter() & end;
    tscBefore = AsmReadTsc();
    MicroSecondDelay(20000);
    tscAfter = AsmReadTsc();
    pmAfter = GetPerformanceCounter() & end;
    ns = GetTimeInNanoSecond(CounterDelta(pmBefore, pmAfter, end));
    ticks = tscAfter - tscBefore;
    if(tscAfter <= tscBefore || ns == 0 || ticks > MAX_UINT64 / 1000000000u)
        return FALSE;
    mFrequency = DivU64x64Remainder(ticks * 1000000000u, ns, NULL);
    if(mFrequency == 0 || mFrequency > 1000000000000ULL)
    {
        mFrequency = 0;
        return FALSE;
    }
    return TRUE;
}

void _plat__TimerReset(void)
{
    if(!ClockInitialize())
        FAIL(FATAL_ERROR_INTERNAL);
    mEpoch = mLastCounter = AsmReadTsc();
    mLastUs = mTpmUs = 0;
    mFraction = 0;
    mRate = CLOCK_NOMINAL;
    mReset = mStopped = TRUE;
}

void _plat__TimerRestart(void)
{
    // Preserve elapsed time and report the discontinuity, as upstream does.
    mStopped = TRUE;
}

// Carry the division remainder so frequent reads cannot lose fractional time.
static void ClockAccumulate(UINT64 deltaUs)
{
    UINT64 whole = deltaUs / mRate;
    UINT64 tail = (deltaUs % mRate) * CLOCK_NOMINAL + mFraction;
    UINT64 advance;
    if(whole > MAX_UINT64 / CLOCK_NOMINAL)
        FAIL(FATAL_ERROR_INTERNAL);
    advance = whole * CLOCK_NOMINAL + tail / mRate;
    mFraction = (UINT32)(tail % mRate);
    if(advance > MAX_UINT64 - mTpmUs)
        FAIL(FATAL_ERROR_INTERNAL);
    mTpmUs += advance;
}

uint64_t _plat__TimerRead(void)
{
    UINT64 counter, ticks, seconds, remainder, us;
    if(mFrequency == 0)
        _plat__TimerReset();
    counter = AsmReadTsc();
    if(counter < mLastCounter)
    {
        // Migration/reset must not turn a negative delta into a huge advance.
        mEpoch = counter;
        mLastUs = 0;
        mStopped = TRUE;
    }
    mLastCounter = counter;
    ticks = counter - mEpoch;
    seconds = DivU64x64Remainder(ticks, mFrequency, &remainder);
    if(seconds > MAX_UINT64 / 1000000u)
        FAIL(FATAL_ERROR_INTERNAL);
    us = seconds * 1000000u + DivU64x64Remainder(remainder * 1000000u, mFrequency, NULL);
    ClockAccumulate(us - mLastUs);
    mLastUs = us;
    return mTpmUs / 1000u;
}

int _plat__TimerWasReset(void)
{
    int value = mReset;
    mReset = FALSE;
    return value;
}

int _plat__TimerWasStopped(void)
{
    int value = mStopped;
    mStopped = FALSE;
    return value;
}

void _plat__ClockRateAdjust(_plat__ClockAdjustStep adjust)
{
    INT32 step = 0;
    INT32 rate;
    // Commit time at the old rate before changing its divisor.
    _plat__TimerRead();
    switch(adjust)
    {
        case PLAT_TPM_CLOCK_ADJUST_COARSE_SLOWER: step = 300; break;
        case PLAT_TPM_CLOCK_ADJUST_MEDIUM_SLOWER: step = 30; break;
        case PLAT_TPM_CLOCK_ADJUST_FINE_SLOWER: step = 1; break;
        case PLAT_TPM_CLOCK_ADJUST_FINE_FASTER: step = -1; break;
        case PLAT_TPM_CLOCK_ADJUST_MEDIUM_FASTER: step = -30; break;
        case PLAT_TPM_CLOCK_ADJUST_COARSE_FASTER: step = -300; break;
        default: return;
    }
    rate = (INT32)mRate + step;
    // Preserve the fractional microsecond across a divisor change.
    if(rate < CLOCK_MIN) rate = CLOCK_MIN;
    if(rate > CLOCK_MAX) rate = CLOCK_MAX;
    mFraction = (UINT32)(((UINT64)mFraction * (UINT32)rate) / mRate);
    mRate = (UINT32)rate;
}

UINT32 EFIAPI VtpmClockSmokeTest(void)
{
    UINT32 passed = 0;
    UINT64 before, after, previous;
    UINTN index;
    BOOL monotonic = TRUE;
    // Lifecycle functions are platform APIs, not declared by the core interface.
    _plat__TimerReset();
    if(_plat__TimerWasReset() && !_plat__TimerWasReset() &&
       _plat__TimerWasStopped() && !_plat__TimerWasStopped())
        passed |= VTPM_CLOCK_TEST_FLAGS;
    before = _plat__TimerRead();
    MicroSecondDelay(20000);
    after = _plat__TimerRead();
    if(after >= before + 10 && after <= before + 1000)
        passed |= VTPM_CLOCK_TEST_ADVANCE;
    previous = after;
    for(index = 0; index < 256; ++index)
    {
        after = _plat__TimerRead();
        if(after < previous) monotonic = FALSE;
        previous = after;
    }
    if(monotonic) passed |= VTPM_CLOCK_TEST_MONOTONIC;
    _plat__TimerRestart();
    if(_plat__TimerWasStopped() && !_plat__TimerWasStopped() &&
       !_plat__TimerWasReset() && _plat__TimerRead() >= previous)
        passed |= VTPM_CLOCK_TEST_RESTART;
    for(index = 0; index < 64; ++index)
        _plat__ClockRateAdjust(PLAT_TPM_CLOCK_ADJUST_COARSE_SLOWER);
    if(mRate == CLOCK_MAX)
    {
        mTpmUs = 0; mFraction = 0;
        ClockAccumulate(35000);
        if(mTpmUs == 30000)
        {
            for(index = 0; index < 64; ++index)
                _plat__ClockRateAdjust(PLAT_TPM_CLOCK_ADJUST_COARSE_FASTER);
            if(mRate == CLOCK_MIN)
            {
                mTpmUs = 0; mFraction = 0;
                for(index = 0; index < 25000; ++index) ClockAccumulate(1);
                if(mTpmUs == 30000 && mFraction == 0)
                    passed |= VTPM_CLOCK_TEST_RATE;
            }
        }
    }
    // Reset probe state before future TPM initialization.
    _plat__TimerReset();
    return passed;
}
