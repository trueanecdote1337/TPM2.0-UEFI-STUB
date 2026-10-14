#include <PiSmm.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/SmmMemLib.h>
#include <Library/SmmServicesTableLib.h>
#include <Protocol/SmmExitBootServices.h>
#include "VtpmProbe.h"
#include <VtpmCoreSmm.h>
#include "VtpmCrb.h"
#include "VtpmControl.h"

STATIC BOOLEAN mAfterExitBootServices;
STATIC VTPM_CORE_SMM_PROTOCOL *mCore;
STATIC UINT8 mTpmResponse[4096];
STATIC CONST UINT8 mGetManufacturer[] = {0x80,1,0,0,0,22,0,0,1,0x7a,
                                        0,0,0,6,0,0,1,5,0,0,0,1};
STATIC CONST UINT8 mGetRandom[] = {0x80,1,0,0,0,12,0,0,1,0x7b,0,32};

STATIC VOID
VtpmProcessTpm (VTPM_PROBE_PACKET *Packet)
{
  UINT32 Size = 0;
  UINT32 RunStatus;
  UINT32 Code;
  UINT32 Rc;
  EFI_STATUS Status;
  // The 64-byte diagnostic mailbox supports these two bounded commands only.
  // A general TPM transport requires full-sized command and response buffers.
  if (!((Packet->RequestSize == sizeof (mGetManufacturer) &&
         CompareMem (Packet->Request, mGetManufacturer, sizeof (mGetManufacturer)) == 0) ||
        (Packet->RequestSize == sizeof (mGetRandom) &&
         CompareMem (Packet->Request, mGetRandom, sizeof (mGetRandom)) == 0))) {
    Packet->Result = 2;
    return;
  }
  if (Packet->ResponseCapacity != VTPM_PACKET_CAPACITY) {
    Packet->Result = 3;
    return;
  }
  if (mCore == NULL) {
    Status = gSmst->SmmLocateProtocol (&gVtpmCoreSmmProtocolGuid, NULL, (VOID **)&mCore);
    if (EFI_ERROR (Status)) {
      Packet->Result = 4;
      return;
    }
  }
  if (mCore->Revision != VTPM_CORE_SMM_REVISION ||
      mCore->ResponseCapacity > sizeof (mTpmResponse) || mCore->Submit == NULL) {
    Packet->Result = 4;
    return;
  }
  RunStatus = mCore->Submit (Packet->RequestSize, Packet->Request,
                             sizeof (mTpmResponse), mTpmResponse, &Size);
  Packet->Result = 4;
  if (RunStatus == 0 && Size >= 10 && Size <= sizeof (Packet->Response)) {
    CopyMem (Packet->Response, mTpmResponse, Size);
    Packet->ResponseSize = Size;
    Packet->Result = 0;
    Code = ((UINT32)Packet->Request[6] << 24) | ((UINT32)Packet->Request[7] << 16) |
           ((UINT32)Packet->Request[8] << 8) | Packet->Request[9];
    Rc = ((UINT32)mTpmResponse[6] << 24) | ((UINT32)mTpmResponse[7] << 16) |
         ((UINT32)mTpmResponse[8] << 8) | mTpmResponse[9];
    DEBUG ((DEBUG_INFO, "[VTPM-SMM] TPM command=0x%x rc=0x%x response=%u phase=%a\n",
            Code, Rc, Size, mAfterExitBootServices ? "after-EBS" : "before-EBS"));
  }
  ZeroMem (mTpmResponse, sizeof (mTpmResponse));
}

STATIC EFI_GUID mProbeGuid = VTPM_PROBE_GUID;
STATIC UINT32   mCalls;
STATIC UINT32   mDoorbellCalls;

STATIC EFI_PHYSICAL_ADDRESS mSharedAddress;

STATIC
VOID
VtpmProcessPacket (VOID)
{
  VTPM_PROBE_PACKET Packet;
  UINT32 Request;
  UINT32 Response;
  EFI_STATUS Status;

  if (mSharedAddress == 0) {
    return;
  }
  Status = SmmCopyMemToSmram (&Packet, (VOID *)(UINTN)mSharedAddress,
                              sizeof (Packet));
  if (EFI_ERROR (Status) || Packet.State != VTPM_PACKET_READY) {
    return;
  }

  Packet.ResponseSize = 0;
  Packet.Result = 1;
  ZeroMem (Packet.Response, sizeof (Packet.Response));
  if (Packet.Signature == VTPM_PROBE_SIGNATURE &&
      Packet.Version == VTPM_PROBE_VERSION) {
    if (Packet.RequestSize > sizeof (Packet.Request) ||
        Packet.ResponseCapacity < sizeof (Response) ||
        Packet.ResponseCapacity > sizeof (Packet.Response)) {
      Packet.Result = 3;
    } else if (Packet.RequestSize != sizeof (Request)) {
      VtpmProcessTpm (&Packet);
    } else {
      CopyMem (&Request, Packet.Request, sizeof (Request));
      if (Request == VTPM_PACKET_PING) {
        Response = VTPM_PACKET_PONG;
        CopyMem (Packet.Response, &Response, sizeof (Response));
        Packet.ResponseSize = sizeof (Response);
        Packet.Result = 0;
      } else {
        Packet.Result = 2;
      }
    }
  }
  Packet.ResponseSequence = Packet.Sequence;
  Packet.Calls = mDoorbellCalls;
  Packet.State = VTPM_PACKET_DONE;
  Status = SmmCopyMemFromSmram ((VOID *)(UINTN)mSharedAddress, &Packet,
                                sizeof (Packet));
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] BUFFER sequence=%u result=%u response=%u phase=%a copy=%r\n",
          Packet.Sequence, Packet.Result, Packet.ResponseSize,
          mAfterExitBootServices ? "after-EBS" : "before-EBS", Status));
}

STATIC VTPM_FULL_PACKET mFullPacket;
STATIC volatile UINT32 mFullBusy;

STATIC VOID
VtpmProcessFullPacket (VOID)
{
  EFI_PHYSICAL_ADDRESS Address;
  EFI_STATUS Status;
  UINT32 State;
  UINT32 RunStatus;
  UINT32 Code;
  UINT32 Rc;
  if (mSharedAddress == 0 || InterlockedCompareExchange32 (&mFullBusy, 0, 1) != 0) {
    return;
  }
  Address = mSharedAddress + VTPM_FULL_OFFSET;
  Status = SmmCopyMemToSmram (&mFullPacket, (VOID *)(UINTN)Address, sizeof (mFullPacket));
  if (EFI_ERROR (Status) || mFullPacket.State != VTPM_PACKET_READY) {
    goto Exit;
  }
  mFullPacket.State = VTPM_PACKET_BUSY;
  State = VTPM_PACKET_BUSY;
  Status = SmmCopyMemFromSmram ((VOID *)(UINTN)(Address + OFFSET_OF (VTPM_FULL_PACKET, State)),
                               &State, sizeof (State));
  if (EFI_ERROR (Status)) {
    goto Exit;
  }
  mFullPacket.ResponseSize = 0;
  mFullPacket.Result = 1;
  ZeroMem (mFullPacket.Response, sizeof (mFullPacket.Response));
  if (mFullPacket.Signature != VTPM_FULL_SIGNATURE || mFullPacket.Version != VTPM_FULL_VERSION) {
    goto Complete;
  }
  if (mFullPacket.RequestSize > sizeof (mFullPacket.Request) ||
      mFullPacket.ResponseCapacity > sizeof (mFullPacket.Response)) {
    mFullPacket.Result = 3;
    goto Complete;
  }
  if (mCore == NULL) {
    Status = gSmst->SmmLocateProtocol (&gVtpmCoreSmmProtocolGuid, NULL, (VOID **)&mCore);
    if (EFI_ERROR (Status)) {
      mFullPacket.Result = 4;
      goto Complete;
    }
  }
  if (mCore->Revision != VTPM_CORE_SMM_REVISION || mCore->Submit == NULL ||
      mCore->ResponseCapacity == 0 || mCore->ResponseCapacity > sizeof (mFullPacket.Response) ||
      mCore->CommandCapacity == 0 || mCore->CommandCapacity > sizeof (mFullPacket.Request)) {
    mFullPacket.Result = 4;
    goto Complete;
  }
  // Reject insufficient capacity BEFORE executing any stateful command.
  if (mFullPacket.RequestSize > mCore->CommandCapacity ||
      mFullPacket.ResponseCapacity < mCore->ResponseCapacity) {
    mFullPacket.Result = 3;
    goto Complete;
  }
  RunStatus = mCore->Submit (mFullPacket.RequestSize, mFullPacket.Request,
                             sizeof (mFullPacket.Response), mFullPacket.Response,
                             &mFullPacket.ResponseSize);
  mFullPacket.Result = RunStatus == 0 ? 0 : 4;
  if (RunStatus == 0 && mFullPacket.ResponseSize >= 10 &&
      mFullPacket.ResponseSize <= mCore->ResponseCapacity) {
    Code = mFullPacket.RequestSize >= 10 ?
           ((UINT32)mFullPacket.Request[6] << 24) | ((UINT32)mFullPacket.Request[7] << 16) |
           ((UINT32)mFullPacket.Request[8] << 8) | mFullPacket.Request[9] : 0;
    Rc = ((UINT32)mFullPacket.Response[6] << 24) | ((UINT32)mFullPacket.Response[7] << 16) |
         ((UINT32)mFullPacket.Response[8] << 8) | mFullPacket.Response[9];
    DEBUG ((DEBUG_INFO, "[VTPM-SMM] FULL command=0x%x rc=0x%x response=%u phase=%a\n",
            Code, Rc, mFullPacket.ResponseSize, mAfterExitBootServices ? "after-EBS" : "before-EBS"));
  } else {
    mFullPacket.ResponseSize = 0;
    mFullPacket.Result = 4;
  }
Complete:
  mFullPacket.ResponseSequence = mFullPacket.Sequence;
  mFullPacket.Calls = mDoorbellCalls;
  // Write the result while BUSY; publish DONE only after the bytes are present.
  Status = SmmCopyMemFromSmram ((VOID *)(UINTN)Address, &mFullPacket, sizeof (mFullPacket));
  if (!EFI_ERROR (Status)) {
    State = VTPM_PACKET_DONE;
    Status = SmmCopyMemFromSmram ((VOID *)(UINTN)(Address + OFFSET_OF (VTPM_FULL_PACKET, State)),
                                 &State, sizeof (State));
  }
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] FULL sequence=%u result=%u response=%u phase=%a copy=%r\n",
          mFullPacket.Sequence, mFullPacket.Result, mFullPacket.ResponseSize,
          mAfterExitBootServices ? "after-EBS" : "before-EBS", Status));
Exit:
  // Requests may contain auth values or key material once the full transport is used.
  {
    volatile UINT8 *Cursor = (volatile UINT8 *)&mFullPacket;
    UINTN Remaining = sizeof (mFullPacket);
    while (Remaining-- != 0) { *Cursor++ = 0; }
  }
  InterlockedCompareExchange32 (&mFullBusy, 1, 0);
}

STATIC
EFI_STATUS
EFIAPI
VtpmProbeExitBootServices (
  IN CONST EFI_GUID *Protocol,
  IN VOID *Interface,
  IN EFI_HANDLE Handle
  )
{
  mAfterExitBootServices = TRUE;
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] ExitBootServices observed\n"));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VtpmProbeDoorbell (
  IN EFI_HANDLE DispatchHandle,
  IN CONST VOID *Context OPTIONAL,
  IN OUT VOID *CommBuffer OPTIONAL,
  IN OUT UINTN *CommBufferSize OPTIONAL
  )
{
  BOOLEAN TimerHandled;

  // A timer and APM request can share one SMI entry. Service both sources.
  TimerHandled = VtpmTimerDispatch (mAfterExitBootServices);
  // OVMF has no software-SMI child dispatcher in this build. Filter the root
  // handler explicitly. The request token prevents replay on unrelated SMIs.
  if (IoRead8 (VTPM_PROBE_APM_COMMAND_PORT) != VTPM_PROBE_SMI_COMMAND ||
      (IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_PROBE_SMI_REQUEST &&
       IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_CRB_KICK &&
       IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_CONTROL_ACCEPT &&
       IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_CONTROL_EXECUTE &&
       IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_CONTROL_START &&
       IoRead8 (VTPM_PROBE_APM_DATA_PORT) != VTPM_CONTROL_STOP)) {
    return TimerHandled ? EFI_SUCCESS : EFI_WARN_INTERRUPT_SOURCE_PENDING;
  }

  ++mDoorbellCalls;
  if (IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_CONTROL_STOP) {
    VtpmTimerStop (); VtpmControlAbort ();
  } else if (IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_CONTROL_START) {
    if (!VtpmControlAccept ()) {
      IoWrite8 (VTPM_PROBE_APM_DATA_PORT, VTPM_CONTROL_REJECT);
      return EFI_SUCCESS;
    }
    if (!VtpmTimerArm ()) {
      VtpmControlAbort ();
      IoWrite8 (VTPM_PROBE_APM_DATA_PORT, VTPM_CONTROL_REJECT);
      return EFI_SUCCESS;
    }
    DEBUG ((DEBUG_INFO, "[VTPM-ACPI] Start accepted phase=%a\n", mAfterExitBootServices ? "after-EBS" : "before-EBS"));
  } else if (IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_CONTROL_ACCEPT) {
    IoWrite8 (VTPM_PROBE_APM_DATA_PORT, VtpmControlAccept () ? VTPM_PROBE_SMI_ACK : VTPM_CONTROL_REJECT);
    return EFI_SUCCESS;
  } else if (IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_CONTROL_EXECUTE) {
    VtpmControlExecute (mAfterExitBootServices);
  } else if (IoRead8 (VTPM_PROBE_APM_DATA_PORT) == VTPM_CRB_KICK) {
    VtpmCrbProcess (mAfterExitBootServices);
  } else {
    VtpmProcessPacket ();
    VtpmProcessFullPacket ();
  }
  IoWrite8 (VTPM_PROBE_APM_DATA_PORT, VTPM_PROBE_SMI_ACK);
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] DOORBELL calls=%u phase=%a\n",
          mDoorbellCalls, mAfterExitBootServices ? "after-EBS" : "before-EBS"));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VtpmProbeHandler (
  IN EFI_HANDLE DispatchHandle,
  IN CONST VOID *Context OPTIONAL,
  IN OUT VOID   *CommBuffer OPTIONAL,
  IN OUT UINTN  *CommBufferSize OPTIONAL
  )
{
  VTPM_PROBE_MESSAGE Message;

  if (CommBuffer == NULL || CommBufferSize == NULL ||
      *CommBufferSize != sizeof (Message)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!SmmIsBufferOutsideSmmValid ((EFI_PHYSICAL_ADDRESS)(UINTN)CommBuffer,
                                 sizeof (Message))) {
    return EFI_SECURITY_VIOLATION;
  }

  // Work on a private copy of the untrusted request.
  CopyMem (&Message, CommBuffer, sizeof (Message));
  if (Message.Signature != VTPM_PROBE_SIGNATURE ||
      Message.Version != VTPM_PROBE_VERSION) {
    return EFI_INVALID_PARAMETER;
  }

  if (Message.Operation == VTPM_PROBE_REGISTER_BUFFER) {
    Message.Result = 1;
    // Bind once during firmware initialization; OS requests cannot redirect it.
    if (!mAfterExitBootServices && mSharedAddress == 0 &&
        Message.SharedAddress != 0 &&
        (Message.SharedAddress & (EFI_PAGE_SIZE - 1)) == 0 &&
        SmmIsBufferOutsideSmmValid (Message.SharedAddress, VTPM_SHARED_SIZE)) {
      mSharedAddress = Message.SharedAddress;
      VtpmCrbBind (mSharedAddress);
      VtpmControlBind (mSharedAddress);
      Message.Result = 0;
      DEBUG ((DEBUG_INFO, "[VTPM-SMM] Shared buffer registered: 0x%Lx\n", mSharedAddress));
    }
    CopyMem (CommBuffer, &Message, sizeof (Message));
    return EFI_SUCCESS;
  }
  if (Message.Operation != VTPM_PROBE_PING) {
    return EFI_INVALID_PARAMETER;
  }

  Message.Result = 0;
  Message.Calls  = ++mCalls;
  CopyMem (CommBuffer, &Message, sizeof (Message));
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] PING sequence=%u calls=%u\n",
          Message.Sequence, Message.Calls));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
VtpmProbeSmmEntryPoint (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_HANDLE DispatchHandle;
  EFI_HANDLE DoorbellHandle;
  VOID *Registration;

  VtpmTimerInitialize ();
  Status = gSmst->SmmRegisterProtocolNotify (
                    &gEdkiiSmmExitBootServicesProtocolGuid,
                    VtpmProbeExitBootServices, &Registration);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[VTPM-SMM] FAIL: EBS notification: %r\n", Status));
    return Status;
  }

  Status = gSmst->SmiHandlerRegister (VtpmProbeDoorbell, NULL, &DoorbellHandle);
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] Q35 doorbell registration: %r\n", Status));
  if (EFI_ERROR (Status)) {
    gSmst->SmmRegisterProtocolNotify (&gEdkiiSmmExitBootServicesProtocolGuid,
                                      NULL, &Registration);
    return Status;
  }

  Status = gSmst->SmiHandlerRegister (VtpmProbeHandler, &mProbeGuid,
                                    &DispatchHandle);
  DEBUG ((DEBUG_INFO, "[VTPM-SMM] Handler registration: %r\n", Status));
  if (EFI_ERROR (Status)) {
    gSmst->SmiHandlerUnRegister (DoorbellHandle);
    gSmst->SmmRegisterProtocolNotify (&gEdkiiSmmExitBootServicesProtocolGuid,
                                      NULL, &Registration);
  }
  return Status;
}
