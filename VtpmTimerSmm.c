// QEMU q35 software-SMI timer. Not a physical ICH9 implementation.
// QEMU ich9_timer.c / lpc_ich9.c: GEN_PMCON_3[7:6]=1 => 16 ms.
#include <PiSmm.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/PciLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include "VtpmControl.h"
STATIC UINTN mPm;
STATIC BOOLEAN mArmed;
STATIC BOOLEAN mTraditionalSync;
VOID VtpmTimerInitialize(VOID) {
  // Snapshot dynamic DXE PCD before ReadyToLock; do not read its database
  // from runtime SMM where it is outside the permitted mapped regions.
  mTraditionalSync = PcdGet8(PcdCpuSmmSyncMode) == 0;
  DEBUG((DEBUG_INFO,"[VTPM-SMI] CPU sync snapshot: %a\n",mTraditionalSync ? "traditional" : "relaxed"));
}
STATIC BOOLEAN mRendezvousChecked;
STATIC VOID EFIAPI VtpmApNoop(VOID *Context) { }

BOOLEAN VtpmTimerArm(VOID) {
  UINT16 Rate;
  // Runtime UEFI communication uses a shared InSmm flag. Relaxed sync can
  // expose it to a still-running OS CPU and trigger VariableSmmRuntime ASSERT.
  if (!mTraditionalSync) {
    DEBUG ((DEBUG_ERROR, "[VTPM-SMI] Refusing relaxed CPU sync; use x-smi-broadcast=on\n"));
    return FALSE;
  }
  if(mPm==0) {
    if(PciRead32(PCI_LIB_ADDRESS(0,31,0,0))!=0x29188086) return FALSE;
    mPm=PciRead32(PCI_LIB_ADDRESS(0,31,0,0x40)) & 0xFF80;
    if(mPm==0 || (IoRead32(mPm+0x30)&(BIT0|BIT5))!=(BIT0|BIT5)) { mPm=0; return FALSE; }
  }
  if(mArmed || (IoRead32(mPm+0x30)&BIT6)) return FALSE;
  Rate=PciRead16(PCI_LIB_ADDRESS(0,31,0,0xA4));
  PciWrite16(PCI_LIB_ADDRESS(0,31,0,0xA4),(Rate & ~0xC0)|0x40);
  // QEMU SMI_STS is a masked writable register, not physical ICH9 W1C.
  IoWrite32(mPm+0x34,IoRead32(mPm+0x34)&~BIT6);
  mArmed=TRUE; IoWrite32(mPm+0x30,IoRead32(mPm+0x30)|BIT6);
  return TRUE;
}
BOOLEAN VtpmTimerDispatch(BOOLEAN AfterEbs) {
  UINTN Index; EFI_STATUS Status;
  if(!mArmed || !(IoRead32(mPm+0x34)&BIT6)) return FALSE;
  // Disarm first: the QEMU repeating timer is used as a one-shot worker.
  IoWrite32(mPm+0x30,IoRead32(mPm+0x30)&~BIT6);
  IoWrite32(mPm+0x34,IoRead32(mPm+0x34)&~BIT6);
  mArmed=FALSE;
  if (!AfterEbs && !mRendezvousChecked) {
    // StartupThisAp rejects CPUs that have not arrived in this SMM run.
    // Scheduling a no-op therefore verifies actual presence, not just a PCD.
    for (Index=0;Index<gSmst->NumberOfCpus;++Index) {
      if(Index==gSmst->CurrentlyExecutingCpu) continue;
      Status=gSmst->SmmStartupThisAp(VtpmApNoop,Index,NULL);
      if(EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR,"[VTPM-SMI] FAIL: CPU %u not available in timer SMM: %r\n",(UINT32)Index,Status));
        return TRUE;
      }
    }
    mRendezvousChecked=TRUE;
    DEBUG((DEBUG_INFO,"[VTPM-SMI] PASS: all %u CPUs in SMM before timer worker\n",(UINT32)gSmst->NumberOfCpus));
  }
  VtpmControlExecute(AfterEbs);
  return TRUE;
}
VOID VtpmTimerStop(VOID) {
  if(mPm!=0 && mArmed) {
    IoWrite32(mPm+0x30,IoRead32(mPm+0x30)&~BIT6);
    IoWrite32(mPm+0x34,IoRead32(mPm+0x34)&~BIT6);
    mArmed=FALSE;
  }
}
