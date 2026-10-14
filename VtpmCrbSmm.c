// Diagnostic CRB model. RAM register writes are consumed only on explicit SMI.
// This is not yet a hardware MMIO handler or the Windows-facing TPM interface.
#include <PiSmm.h>
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/SmmMemLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/SmmServicesTableLib.h>
#include "VtpmCrb.h"

STATIC EFI_PHYSICAL_ADDRESS mBase;
STATIC volatile UINT32 mBusy;
STATIC BOOLEAN mGranted;
STATIC BOOLEAN mReady;
STATIC BOOLEAN mFatal;
STATIC PTP_CRB_REGISTERS mInput;
STATIC PTP_CRB_REGISTERS mOutput;
STATIC VTPM_CORE_SMM_PROTOCOL *mOwner;
STATIC UINT8 mCommand[VTPM_FULL_CAPACITY];
STATIC UINT8 mResponse[VTPM_FULL_CAPACITY];

STATIC UINT32 ReadBe32(CONST UINT8 *Data)
{
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) |
         ((UINT32)Data[2] << 8) | Data[3];
}
STATIC VOID ErrorResponse(UINT32 Rc)
{
  ZeroMem(mResponse, sizeof(mResponse));
  mResponse[0] = 0x80; mResponse[1] = 1; mResponse[5] = 10;
  mResponse[6] = (UINT8)(Rc >> 24); mResponse[7] = (UINT8)(Rc >> 16);
  mResponse[8] = (UINT8)(Rc >> 8); mResponse[9] = (UINT8)Rc;
}
STATIC VOID Publish(VOID)
{
  PTP_CRB_INTERFACE_IDENTIFIER Id;
  UINT64 CmdAddress = mBase + VTPM_CRB_COMMAND_OFFSET;
  EFI_STATUS Status;
  ZeroMem(&mOutput, sizeof(mOutput));
  Id.Uint32 = 0;
  Id.Bits.InterfaceType = PTP_INTERFACE_IDENTIFIER_INTERFACE_TYPE_CRB;
  Id.Bits.InterfaceVersion = PTP_INTERFACE_IDENTIFIER_INTERFACE_VERSION_CRB;
  Id.Bits.CapCRB = 1;
  Id.Bits.InterfaceSelector = PTP_INTERFACE_IDENTIFIER_INTERFACE_SELECTOR_CRB;
  Id.Bits.IntfSelLock = 1;
  mOutput.InterfaceId = Id.Uint32;
  mOutput.LocalityState = PTP_CRB_LOCALITY_STATE_TPM_REG_VALID_STATUS |
                         PTP_CRB_LOCALITY_STATE_TPM_ESTABLISHED |
                         (mGranted ? PTP_CRB_LOCALITY_STATE_LOCALITY_ASSIGNED : 0);
  mOutput.LocalityStatus = mGranted ? PTP_CRB_LOCALITY_STATUS_GRANTED : 0;
  mOutput.CrbControlStatus = (mReady ? 0 : PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE) |
                            (mFatal ? PTP_CRB_CONTROL_AREA_STATUS_TPM_STATUS : 0);
  mOutput.CrbControlCancel = mInput.CrbControlCancel & PTP_CRB_CONTROL_CANCEL;
  mOutput.CrbControlCommandAddressLow = (UINT32)CmdAddress;
  mOutput.CrbControlCommandAddressHigh = (UINT32)(CmdAddress >> 32);
  mOutput.CrbControlResponseAddrss = mBase + VTPM_CRB_RESPONSE_OFFSET;
  if (mOwner != NULL) {
    mOutput.CrbControlCommandSize = mOwner->CommandCapacity;
    mOutput.CrbControlResponseSize = mOwner->ResponseCapacity;
  }
  // Response bytes are copied BEFORE Start is cleared in this publication.
  Status = SmmCopyMemFromSmram((VOID *)(UINTN)(mBase + VTPM_CRB_OFFSET),
                              &mOutput, VTPM_CRB_HEADER_SIZE);
  if (EFI_ERROR(Status)) mFatal = TRUE;
}
VOID VtpmCrbBind(EFI_PHYSICAL_ADDRESS Address)
{
  EFI_STATUS Status;
  mBase = Address; mReady = mGranted = mFatal = FALSE;
  ZeroMem(&mInput, sizeof(mInput));
  Status = gSmst->SmmLocateProtocol(&gVtpmCoreSmmProtocolGuid, NULL, (VOID **)&mOwner);
  if (EFI_ERROR(Status) || mOwner->Revision != VTPM_CORE_SMM_REVISION ||
      mOwner->Submit == NULL || mOwner->CommandCapacity < 10 ||
      mOwner->ResponseCapacity < 10 || mOwner->CommandCapacity > sizeof(mCommand) ||
      mOwner->ResponseCapacity > sizeof(mResponse)) {
    mOwner = NULL; mFatal = TRUE;
  }
  Publish();
}
VOID VtpmCrbProcess(BOOLEAN AfterEbs)
{
  EFI_STATUS Status;
  UINT32 Size;
  UINT32 ResponseSize;
  UINT32 Result;
  UINT32 Rc;
  if (mBase == 0 || InterlockedCompareExchange32(&mBusy, 0, 1) != 0) return;
  Status = SmmCopyMemToSmram(&mInput, (VOID *)(UINTN)(mBase + VTPM_CRB_OFFSET), VTPM_CRB_HEADER_SIZE);
  if (EFI_ERROR(Status)) goto Exit;
  if (!mFatal) {
    if (mInput.LocalityControl & PTP_CRB_LOCALITY_CONTROL_RELINQUISH) {
      mGranted = FALSE; mReady = FALSE;
    } else if (mInput.LocalityControl & PTP_CRB_LOCALITY_CONTROL_REQUEST_ACCESS) {
      mGranted = TRUE;
    }
    if (mGranted) {
      if (mInput.CrbControlRequest & PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE) mReady = FALSE;
      else if (mInput.CrbControlRequest & PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY) mReady = TRUE;
    }
    if ((mInput.CrbControlStart & BIT0) && mGranted && mReady) {
      ZeroMem(mResponse, sizeof(mResponse));
      if (mInput.CrbControlCancel & PTP_CRB_CONTROL_CANCEL) {
        ErrorResponse(TPM_RC_CANCELED); ResponseSize = 10;
      } else {
        // Addresses/sizes written to the RAM registers are NEVER dereferenced.
        Status = SmmCopyMemToSmram(mCommand, (VOID *)(UINTN)(mBase + VTPM_CRB_COMMAND_OFFSET), sizeof(mCommand));
        if (EFI_ERROR(Status)) { mFatal = TRUE; goto Done; }
        Size = ReadBe32(mCommand + 2);
        if (Size < 10 || Size > mOwner->CommandCapacity) {
          ErrorResponse(TPM_RC_COMMAND_SIZE); ResponseSize = 10;
        } else {
          Result = mOwner->Submit(Size, mCommand, sizeof(mResponse), mResponse, &ResponseSize);
          if (Result != 0 || ResponseSize < 10 || ResponseSize > mOwner->ResponseCapacity) {
            mFatal = TRUE; goto Done;
          }
        }
      }
      Status = SmmCopyMemFromSmram((VOID *)(UINTN)(mBase + VTPM_CRB_RESPONSE_OFFSET), mResponse, sizeof(mResponse));
      if (EFI_ERROR(Status)) { mFatal = TRUE; goto Done; }
      Rc = ReadBe32(mResponse + 6);
      DEBUG((DEBUG_INFO, "[VTPM-CRB] command complete rc=0x%x response=%u phase=%a\n",
             Rc, ResponseSize, AfterEbs ? "after-EBS" : "before-EBS"));
    }
  }
Done:
  Publish();
Exit:
  {
    volatile UINT8 *Cursor;
    UINTN Left;
    Cursor = mCommand; Left = sizeof(mCommand); while(Left-- != 0) *Cursor++ = 0;
    Cursor = mResponse; Left = sizeof(mResponse); while(Left-- != 0) *Cursor++ = 0;
  }
  InterlockedCompareExchange32(&mBusy, 1, 0);
}
