# Run in the Windows guest after rebuilding OVMF. Read-only TPM commands only.
# Microsoft TBS API: Tbsi_GetDeviceInfo / Tbsi_Context_Create / Tbsip_Submit_Command.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VtpmTbsProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct DeviceInfo {
        public UInt32 structVersion, tpmVersion, tpmInterfaceType, tpmImpRevision;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct ContextParams { public UInt32 version, flags; }
    [DllImport("tbs.dll", ExactSpelling=true)]
    public static extern UInt32 Tbsi_GetDeviceInfo(UInt32 size, out DeviceInfo info);
    [DllImport("tbs.dll", ExactSpelling=true)]
    public static extern UInt32 Tbsi_Context_Create(ref ContextParams parameters, out IntPtr context);
    [DllImport("tbs.dll", ExactSpelling=true)]
    public static extern UInt32 Tbsip_Context_Close(IntPtr context);
    [DllImport("tbs.dll", ExactSpelling=true)]
    public static extern UInt32 Tbsip_Submit_Command(IntPtr context, UInt32 locality,
        UInt32 priority, byte[] command, UInt32 commandSize, [Out] byte[] response,
        ref UInt32 responseSize);
    public static DeviceInfo GetInfo() {
        DeviceInfo info;
        UInt32 result=Tbsi_GetDeviceInfo(16,out info);
        if(result!=0) throw new Exception(String.Format("Tbsi_GetDeviceInfo failed: 0x{0:X8}",result));
        return info;
    }
    public static IntPtr OpenContext() {
        ContextParams parameters=new ContextParams();
        parameters.version=2; parameters.flags=4;
        IntPtr context;
        UInt32 result=Tbsi_Context_Create(ref parameters,out context);
        if(result!=0) throw new Exception(String.Format("Tbsi_Context_Create failed: 0x{0:X8}",result));
        return context;
    }
    private static UInt32 Be32(byte[] bytes, int offset) {
        return ((UInt32)bytes[offset]<<24)|((UInt32)bytes[offset+1]<<16)|
               ((UInt32)bytes[offset+2]<<8)|bytes[offset+3];
    }
    public static string Submit(IntPtr context, byte[] command, bool random) {
        byte[] response=new byte[4096]; UInt32 size=(UInt32)response.Length;
        UInt32 result=Tbsip_Submit_Command(context,0,200,command,(UInt32)command.Length,response,ref size);
        if(result!=0) throw new Exception(String.Format("TBS submit failed: 0x{0:X8}",result));
        if(size<10 || size>response.Length || Be32(response,2)!=size)
            throw new Exception("Malformed TPM response length");
        UInt32 rc=Be32(response,6);
        if(rc!=0) throw new Exception(String.Format("TPM response RC: 0x{0:X}",rc));
        if(response[0]!=0x80 || response[1]!=1) throw new Exception("Unexpected TPM tag");
        if(random) {
            if(size<12) throw new Exception("Truncated random response");
            UInt32 count=((UInt32)response[10]<<8)|response[11];
            if(count==0 || count>32 || count+12!=size) throw new Exception("Invalid random TPM2B length");
            return String.Format("PASS GetRandom: rc=0, randomBytes={0}",count);
        }
        if(size!=27 || response[10]>1 || Be32(response,11)!=6 ||
           Be32(response,15)!=1 || Be32(response,19)!=0x105 ||
           System.Text.Encoding.ASCII.GetString(response,23,4)!="VTPM")
            throw new Exception("Unexpected manufacturer capability");
        return "PASS GetCapability: rc=0, manufacturer=VTPM";
    }
}
'@
# Create native structs inside C#; PowerShell need not resolve nested type names.
$info = [VtpmTbsProbe]::GetInfo()
$info | Format-List
if ($info.tpmVersion -ne 2) { throw 'TPM 2.0 was not detected' }
$context = [VtpmTbsProbe]::OpenContext()
try {
    [byte[]]$cap = 0x80,1,0,0,0,22,0,0,1,0x7A,0,0,0,6,0,0,1,5,0,0,0,1
    [byte[]]$random = 0x80,1,0,0,0,12,0,0,1,0x7B,0,32
    [VtpmTbsProbe]::Submit($context, $cap, $false)
    [VtpmTbsProbe]::Submit($context, $random, $true)
    'PASS: Windows TBS -> tpm.sys -> ACPI -> SMM -> TPM core'
} finally {
    $closed = [VtpmTbsProbe]::Tbsip_Context_Close($context)
    if ($closed -ne 0) { Write-Warning ('Context close failed: 0x{0:X8}' -f $closed) }
}
