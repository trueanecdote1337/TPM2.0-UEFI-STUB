// ACPI StartMethod=2 backend: accept snapshots the command; timer executes it.
#include <PiSmm.h>
#include <IndustryStandard/Tpm20.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/SmmMemLib.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/SynchronizationLib.h>
#include "VtpmControl.h"
STATIC EFI_PHYSICAL_ADDRESS mBase;
STATIC VTPM_CORE_SMM_PROTOCOL *mOwner;
STATIC BOOLEAN mPending;
STATIC volatile UINT32 mBusy;
STATIC UINT8 mCommand[VTPM_FULL_CAPACITY];
STATIC UINT8 mResponse[VTPM_FULL_CAPACITY];
STATIC UINT32 mSize;
STATIC UINT32 mCancel;
STATIC UINT32 Be32(CONST UINT8 *P) {
  return ((UINT32)P[0]<<24)|((UINT32)P[1]<<16)|((UINT32)P[2]<<8)|P[3];
}
STATIC VOID Wipe(VOID) {
  volatile UINT8 *P; UINTN N;
  P=mCommand; N=sizeof(mCommand); while(N-- != 0) *P++=0;
  P=mResponse; N=sizeof(mResponse); while(N-- != 0) *P++=0;
}
VOID VtpmControlAbort(VOID) {
  if(InterlockedCompareExchange32(&mBusy,0,1)!=0) return;
  mPending=FALSE; Wipe();
  InterlockedCompareExchange32(&mBusy,1,0);
}
STATIC EFI_STATUS Publish(UINT32 Error, UINT32 Start) {
  EFI_TPM2_ACPI_CONTROL_AREA C;
  ZeroMem(&C,sizeof(C)); C.Error=Error; C.Start=Start; C.Cancel=mCancel;
  C.Command=mBase+VTPM_CONTROL_COMMAND_OFFSET;
  C.Response=mBase+VTPM_CONTROL_RESPONSE_OFFSET;
  if(mOwner != NULL) { C.CommandSize=mOwner->CommandCapacity; C.ResponseSize=mOwner->ResponseCapacity; }
  return SmmCopyMemFromSmram((VOID *)(UINTN)(mBase+VTPM_CONTROL_OFFSET),&C,sizeof(C));
}
VOID VtpmControlBind(EFI_PHYSICAL_ADDRESS Address) {
  EFI_STATUS S;
  mBase=Address; mPending=FALSE;
  S=gSmst->SmmLocateProtocol(&gVtpmCoreSmmProtocolGuid,NULL,(VOID **)&mOwner);
  if(EFI_ERROR(S) || mOwner->Revision != VTPM_CORE_SMM_REVISION || mOwner->Submit==NULL ||
     mOwner->CommandCapacity<10 || mOwner->ResponseCapacity<10 ||
     mOwner->CommandCapacity>sizeof(mCommand) || mOwner->ResponseCapacity>sizeof(mResponse)) mOwner=NULL;
  Publish(mOwner==NULL ? 1 : 0,0);
}
BOOLEAN VtpmControlAccept(VOID) {
  EFI_TPM2_ACPI_CONTROL_AREA C;
  BOOLEAN Accepted=FALSE;
  if(mBase==0 || InterlockedCompareExchange32(&mBusy,0,1)!=0) return FALSE;
  if(mPending || mOwner==NULL) goto Exit;
  if(EFI_ERROR(SmmCopyMemToSmram(&C,(VOID *)(UINTN)(mBase+VTPM_CONTROL_OFFSET),sizeof(C)))) goto Exit;
  // Firmware-owned addresses/capacities are checked, never dereferenced from OS data.
  if(C.Start!=1 || C.Error!=0 || C.Reserved!=0 || C.InterruptControl!=0 || C.Cancel>1 ||
     C.Command!=mBase+VTPM_CONTROL_COMMAND_OFFSET || C.Response!=mBase+VTPM_CONTROL_RESPONSE_OFFSET ||
     C.CommandSize!=mOwner->CommandCapacity || C.ResponseSize!=mOwner->ResponseCapacity) goto Exit;
  if(EFI_ERROR(SmmCopyMemToSmram(mCommand,(VOID *)(UINTN)(mBase+VTPM_CONTROL_COMMAND_OFFSET),sizeof(mCommand)))) goto Exit;
  mSize=Be32(mCommand+2); mCancel=C.Cancel;
  mPending=TRUE; Accepted=TRUE;
Exit:
  if(!Accepted && !mPending) Wipe();
  InterlockedCompareExchange32(&mBusy,1,0); return Accepted;
}
VOID VtpmControlExecute(BOOLEAN AfterEbs) {
  UINT32 Rc, Size, Result, Code, Cancel; EFI_STATUS S;
  if(mBase==0 || InterlockedCompareExchange32(&mBusy,0,1)!=0) return;
  if(!mPending) goto Exit;
  // Observe cancellation that arrived while waiting for the timer; pointers
  // remain private and fixed. SMM execution can also finish normally.
  Cancel=0;
  S=SmmCopyMemToSmram(&Cancel,(VOID *)(UINTN)(mBase+VTPM_CONTROL_OFFSET+8),sizeof(Cancel));
  if(!EFI_ERROR(S) && Cancel!=0) mCancel=1;
  ZeroMem(mResponse,sizeof(mResponse));
  Rc=mCancel ? TPM_RC_CANCELED : ((mSize<10 || mSize>mOwner->CommandCapacity) ? TPM_RC_COMMAND_SIZE : 0);
  Result=0; Size=10; Code=mSize>=10 ? Be32(mCommand+6) : 0;
  if(Rc!=0) {
    mResponse[0]=0x80; mResponse[1]=1; mResponse[5]=10;
    mResponse[6]=(UINT8)(Rc>>24); mResponse[7]=(UINT8)(Rc>>16);
    mResponse[8]=(UINT8)(Rc>>8); mResponse[9]=(UINT8)Rc;
  } else Result=mOwner->Submit(mSize,mCommand,sizeof(mResponse),mResponse,&Size);
  if(Result==0 && Size>=10 && Size<=mOwner->ResponseCapacity) {
    S=SmmCopyMemFromSmram((VOID *)(UINTN)(mBase+VTPM_CONTROL_RESPONSE_OFFSET),mResponse,sizeof(mResponse));
    if(EFI_ERROR(S)) Result=1;
  } else Result=1;
  // Completion is published only after the response bytes, keeping fixed sizes.
  Publish(Result==0 ? 0 : 1,0);
  DEBUG((DEBUG_INFO,"[VTPM-CTRL] command=0x%x rc=0x%x response=%u result=%u phase=%a\n",
         Code,Result==0 ? Be32(mResponse+6) : 0,Size,Result,AfterEbs ? "after-EBS" : "before-EBS"));
  mPending=FALSE; Wipe();
Exit:
  InterlockedCompareExchange32(&mBusy,1,0);
}
