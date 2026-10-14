#include <Uefi.h>
#include <Protocol/SmmControl2.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "VtpmControl.h"
STATIC BOOLEAN Ring(EFI_SMM_CONTROL2_PROTOCOL *Control, UINT8 Token, UINT8 Ack) {
  UINT8 Command=VTPM_PROBE_SMI_COMMAND;
  return !EFI_ERROR(Control->Trigger(Control,&Command,&Token,FALSE,0)) &&
         IoRead8(VTPM_PROBE_APM_DATA_PORT)==Ack;
}
EFI_STATUS EFIAPI VtpmControlTransportTest(EFI_PHYSICAL_ADDRESS Base, EFI_SMM_CONTROL2_PROTOCOL *Control) {
  EFI_TPM2_ACPI_CONTROL_AREA *C=(VOID *)(UINTN)(Base+VTPM_CONTROL_OFFSET);
  UINT8 *Cmd=(VOID *)(UINTN)(Base+VTPM_CONTROL_COMMAND_OFFSET);
  UINT8 *Rsp=(VOID *)(UINTN)(Base+VTPM_CONTROL_RESPONSE_OFFSET);
  UINT32 Test; UINT64 Address;
  STATIC CONST UINT8 Rand[]={0x80,1,0,0,0,12,0,0,1,0x7b,0,32};
  if(C->Error || C->Start || C->Command!=Base+VTPM_CONTROL_COMMAND_OFFSET ||
     C->Response!=Base+VTPM_CONTROL_RESPONSE_OFFSET || C->CommandSize<12 || C->ResponseSize<44) goto Fail;
  DEBUG((DEBUG_INFO,"[VTPM-CTRL] PASS: independent control area and buffers\n"));
  for(Test=1;Test<=4;++Test) {
    CopyMem(Cmd,Rand,sizeof(Rand)); SetMem(Rsp,VTPM_FULL_CAPACITY,0x5A);
    C->Start=1; C->Cancel=Test==2;
    if(Test==3) { Cmd[2]=0; Cmd[3]=0; Cmd[4]=0x20; Cmd[5]=0; }
    if(Test==4) {
      Address=C->Command; C->Command=MAX_UINT64;
      if(!Ring(Control,VTPM_CONTROL_ACCEPT,VTPM_CONTROL_REJECT) || C->Start!=1 || Rsp[0]!=0x5A) goto Fail;
      C->Command=Address;
    }
    if(!Ring(Control,VTPM_CONTROL_ACCEPT,VTPM_PROBE_SMI_ACK) || C->Start!=1 || Rsp[0]!=0x5A) goto Fail;
    // A second request must neither replace nor execute the pending snapshot.
    SetMem(Cmd,VTPM_FULL_CAPACITY,0xFF);
    if(!Ring(Control,VTPM_CONTROL_ACCEPT,VTPM_CONTROL_REJECT) || C->Start!=1 || Rsp[0]!=0x5A) goto Fail;
    if(!Ring(Control,VTPM_CONTROL_EXECUTE,VTPM_PROBE_SMI_ACK) || C->Start!=0 || C->Error!=0 ||
       C->Command!=Base+VTPM_CONTROL_COMMAND_OFFSET || C->Response!=Base+VTPM_CONTROL_RESPONSE_OFFSET ||
       Rsp[0]!=0x80 || Rsp[1]!=1 || Rsp[2]!=0 || Rsp[3]!=0 || Rsp[4]!=0 ||
       Rsp[5]!=(Test==2 || Test==3 ? 10 : 44) || Rsp[6]!=0 || Rsp[7]!=0 ||
       Rsp[8]!=(Test==2 ? 9 : (Test==3 ? 1 : 0)) ||
       Rsp[9]!=(Test==2 ? 9 : (Test==3 ? 0x42 : 0)) ||
       ((Test==1 || Test==4) && (Rsp[10]!=0 || Rsp[11]!=32))) goto Fail;
    DEBUG((DEBUG_INFO,"[VTPM-CTRL] PASS: two-phase test=%u\n",Test));
  }
  // No queued command: another worker entry must leave the completed response.
  if(!Ring(Control,VTPM_CONTROL_EXECUTE,VTPM_PROBE_SMI_ACK) || Rsp[5]!=44 || C->Start!=0) goto Fail;
  DEBUG((DEBUG_INFO,"[VTPM-CTRL] PASS: snapshot, deferred execution and recovery verified\n"));
  return EFI_SUCCESS;
Fail:
  DEBUG((DEBUG_ERROR,"[VTPM-CTRL] FAIL: two-phase control area\n"));
  return EFI_DEVICE_ERROR;
}

EFI_STATUS EFIAPI VtpmWindowsTransportTest(EFI_PHYSICAL_ADDRESS Base, EFI_SMM_CONTROL2_PROTOCOL *Control) {
  volatile EFI_TPM2_ACPI_CONTROL_AREA *C=(VOID *)(UINTN)(Base+VTPM_CONTROL_OFFSET);
  UINT8 *Cmd=(VOID *)(UINTN)(Base+VTPM_CONTROL_COMMAND_OFFSET);
  UINT8 *Rsp=(VOID *)(UINTN)(Base+VTPM_CONTROL_RESPONSE_OFFSET);
  UINTN Tick;
  UINT64 Before, After, First, Last, Delta, ElapsedUs;
  STATIC CONST UINT8 Rand[]={0x80,1,0,0,0,12,0,0,1,0x7b,0,32};
  CopyMem(Cmd,Rand,sizeof(Rand)); ZeroMem(Rsp,VTPM_FULL_CAPACITY);
  C->Cancel=0; C->Start=1;
  GetPerformanceCounterProperties(&First,&Last);
  Before=GetPerformanceCounter();
  if(!Ring(Control,VTPM_CONTROL_START,VTPM_PROBE_SMI_ACK)) goto Fail;
  // No manual Execute SMI: the OS-style poll must see timer completion.
  for(Tick=0;Tick<2000 && C->Start!=0;++Tick) gBS->Stall(1000);
  if(C->Start || C->Error || Rsp[5]!=44 || Rsp[6] || Rsp[7] || Rsp[8] || Rsp[9] || Rsp[11]!=32) goto Fail;
  After=GetPerformanceCounter();
  Delta=After>=Before ? After-Before : Last-Before+After-First+1;
  ElapsedUs=DivU64x32(GetTimeInNanoSecond(Delta),1000);
  DEBUG((DEBUG_INFO,"[VTPM-WIN] deferred round-trip=%Lu us\n",ElapsedUs));
  // Catch the broadcast/unicast AP rendezvous penalty before Windows can send
  // dozens of requests. Keep the existing CPU rendezvous policy intact.
  if(ElapsedUs>250000) {
    DEBUG((DEBUG_ERROR,"[VTPM-WIN] FAIL: deferred SMI too slow; require broadcast=on and VtpmSmmCpuFeaturesLib\n"));
    goto Fail;
  }
  DEBUG((DEBUG_INFO,"[VTPM-WIN] PASS: automatic deferred SMI completion\n"));
  return EFI_SUCCESS;
Fail:
  Ring(Control,VTPM_CONTROL_STOP,VTPM_PROBE_SMI_ACK);
  C->Start=0;
  DEBUG((DEBUG_ERROR,"[VTPM-WIN] FAIL: deferred worker unavailable or slow; Windows TPM will not be published\n"));
  return EFI_DEVICE_ERROR;
}
