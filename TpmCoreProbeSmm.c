#include <PiSmm.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <TpmCoreProbe.h>
#include <TpmCoreRun.h>
#include <VtpmCoreSmm.h>
#include <Library/SmmServicesTableLib.h>

STATIC BASE_LIBRARY_JUMP_BUFFER mFailureJump;
STATIC VTPM_CORE_SMM_PROTOCOL mCoreProtocol = { VTPM_CORE_SMM_REVISION, 0, 0, VtpmCoreSubmit };

EFI_STATUS
EFIAPI
TpmCoreProbeSmmEntryPoint (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  UINT32 Passed;
  UINT32 Stage;
  EFI_STATUS Status;
  EFI_HANDLE ProtocolHandle = NULL;

  DEBUG ((DEBUG_INFO, "[VTPM-CORE] Isolated core smoke test starting\n"));
  VtpmCoreProbeBoundary (&mFailureJump);
  if (SetJump (&mFailureJump) != 0) {
    DEBUG ((DEBUG_ERROR, "[VTPM-CORE] FAIL: upstream TPM failure caught; boot continues\n"));
    VtpmCoreProbeBoundary (NULL);
    return EFI_SUCCESS;
  }
  Passed = VtpmCoreSmokeTest ();
  DEBUG ((DEBUG_INFO, "[VTPM-CORE] marshal=%a bounds=%a profile=%a sha256=%a aes128=%a\n",
          (Passed & VTPM_CORE_TEST_MARSHAL) ? "PASS" : "FAIL",
          (Passed & VTPM_CORE_TEST_BOUNDS) ? "PASS" : "FAIL",
          (Passed & VTPM_CORE_TEST_PROFILE) ? "PASS" : "FAIL",
          (Passed & VTPM_CORE_TEST_SHA256) ? "PASS" : "FAIL",
          (Passed & VTPM_CORE_TEST_AES128) ? "PASS" : "FAIL"));
  if (Passed == VTPM_CORE_TEST_ALL) {
    DEBUG ((DEBUG_INFO, "[VTPM-CORE] PASS: isolated core smoke test; boot continues\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "[VTPM-CORE] FAIL: test mask=0x%x; boot continues\n", Passed));
  }
  Passed = VtpmNvSmokeTest ();
  DEBUG ((DEBUG_INFO, "[VTPM-NV] erase=%a read-write=%a overlap=%a bounds=%a lifecycle=%a\n",
          (Passed & VTPM_NV_TEST_ERASE) ? "PASS" : "FAIL",
          (Passed & VTPM_NV_TEST_READ_WRITE) ? "PASS" : "FAIL",
          (Passed & VTPM_NV_TEST_OVERLAP) ? "PASS" : "FAIL",
          (Passed & VTPM_NV_TEST_BOUNDS) ? "PASS" : "FAIL",
          (Passed & VTPM_NV_TEST_LIFECYCLE) ? "PASS" : "FAIL"));
  DEBUG ((Passed == VTPM_NV_TEST_ALL ? DEBUG_INFO : DEBUG_ERROR,
          "[VTPM-NV] %a: volatile SMM storage; no persistence; mask=0x%x\n",
          Passed == VTPM_NV_TEST_ALL ? "PASS" : "FAIL", Passed));
  Passed = VtpmEntropySmokeTest ();
  DEBUG ((DEBUG_INFO, "[VTPM-RNG] init=%a short=%a partial=%a repeat=%a\n",
          (Passed & VTPM_ENTROPY_TEST_INIT) ? "PASS" : "FAIL",
          (Passed & VTPM_ENTROPY_TEST_SHORT) ? "PASS" : "FAIL",
          (Passed & VTPM_ENTROPY_TEST_PARTIAL) ? "PASS" : "FAIL",
          (Passed & VTPM_ENTROPY_TEST_REPEAT) ? "PASS" : "FAIL"));
  DEBUG ((Passed == VTPM_ENTROPY_TEST_ALL ? DEBUG_INFO : DEBUG_ERROR,
          "[VTPM-RNG] %a: RDRAND source; mask=0x%x\n",
          Passed == VTPM_ENTROPY_TEST_ALL ? "PASS" : "FAIL", Passed));
  Passed = VtpmClockSmokeTest ();
  DEBUG ((DEBUG_INFO, "[VTPM-CLOCK] flags=%a advance=%a monotonic=%a restart=%a rate=%a\n",
          (Passed & VTPM_CLOCK_TEST_FLAGS) ? "PASS" : "FAIL",
          (Passed & VTPM_CLOCK_TEST_ADVANCE) ? "PASS" : "FAIL",
          (Passed & VTPM_CLOCK_TEST_MONOTONIC) ? "PASS" : "FAIL",
          (Passed & VTPM_CLOCK_TEST_RESTART) ? "PASS" : "FAIL",
          (Passed & VTPM_CLOCK_TEST_RATE) ? "PASS" : "FAIL"));
  DEBUG ((Passed == VTPM_CLOCK_TEST_ALL ? DEBUG_INFO : DEBUG_ERROR,
          "[VTPM-CLOCK] %a: calibrated TSC; mask=0x%x\n",
          Passed == VTPM_CLOCK_TEST_ALL ? "PASS" : "FAIL", Passed));
  Passed = VtpmPlatformSmokeTest ();
  DEBUG ((DEBUG_INFO, "[VTPM-PLAT] info=%a pcr-profile=%a pcr-values=%a pcr-bounds=%a signals=%a\n",
          (Passed & VTPM_PLATFORM_TEST_INFO) ? "PASS" : "FAIL",
          (Passed & VTPM_PLATFORM_TEST_PCR_PROFILE) ? "PASS" : "FAIL",
          (Passed & VTPM_PLATFORM_TEST_PCR_VALUES) ? "PASS" : "FAIL",
          (Passed & VTPM_PLATFORM_TEST_PCR_BOUNDS) ? "PASS" : "FAIL",
          (Passed & VTPM_PLATFORM_TEST_SIGNALS) ? "PASS" : "FAIL"));
  DEBUG ((Passed == VTPM_PLATFORM_TEST_ALL ? DEBUG_INFO : DEBUG_ERROR,
          "[VTPM-PLAT] %a: platform callbacks; mask=0x%x\n",
          Passed == VTPM_PLATFORM_TEST_ALL ? "PASS" : "FAIL", Passed));
  VtpmCoreProbeBoundary (NULL);
  Passed = VtpmCommandSmokeTest (&Stage);
  DEBUG ((DEBUG_INFO, "[VTPM-CMD] init=%a startup=%a capability=%a selftest=%a random=%a errors=%a\n",
          (Passed & VTPM_COMMAND_TEST_INIT) ? "PASS" : "FAIL",
          (Passed & VTPM_COMMAND_TEST_STARTUP) ? "PASS" : "FAIL",
          (Passed & VTPM_COMMAND_TEST_CAPABILITY) ? "PASS" : "FAIL",
          (Passed & VTPM_COMMAND_TEST_SELFTEST) ? "PASS" : "FAIL",
          (Passed & VTPM_COMMAND_TEST_RANDOM) ? "PASS" : "FAIL",
          (Passed & VTPM_COMMAND_TEST_ERRORS) ? "PASS" : "FAIL"));
  DEBUG ((Passed == VTPM_COMMAND_TEST_ALL ? DEBUG_INFO : DEBUG_ERROR,
          "[VTPM-CMD] %a: ExecuteCommand; mask=0x%x stage=%u\n",
          Passed == VTPM_COMMAND_TEST_ALL ? "PASS" : "FAIL", Passed, Stage));
  if (Passed == VTPM_COMMAND_TEST_ALL) {
    mCoreProtocol.CommandCapacity = VtpmCoreCommandCapacity ();
    mCoreProtocol.ResponseCapacity = VtpmCoreResponseCapacity ();
    Status = gSmst->SmmInstallProtocolInterface (&ProtocolHandle,
                  &gVtpmCoreSmmProtocolGuid, EFI_NATIVE_INTERFACE, &mCoreProtocol);
    DEBUG ((EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
            "[VTPM-CMD] SMM core protocol installation: %r\n", Status));
  }
  return EFI_SUCCESS;
}
