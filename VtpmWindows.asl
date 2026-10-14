// QEMU q35 firmware TPM: Microsoft ACPI StartMethod=2.
DefinitionBlock ("VtpmWindows.aml", "SSDT", 2, "VTPM2 ", "VTPMWIN ", 1)
{
    External (\_SB.VPRB.MBXL, MutexObj)
    Scope (\_SB)
    {
        Device (VTPM)
        {
            Name (_HID, "MSFT0101")
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_STR, Unicode ("EDK II firmware TPM 2.0"))
            Name (TBAS, 0xF0E1D2C3B4A59687)
            Name (RBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0, 0x3000, MRSC)
            })
            CreateDWordField (RBUF, MRSC._BAS, RBAS)
            Method (_CRS, 0, Serialized)
            {
                Store (TBAS, RBAS)
                Return (RBUF)
            }
            // B2/B3 belong to the platform SMI reservation (QEMU SMI0).
            // AML accesses them; tpm.sys must not claim them as device resources.
            OperationRegion (APMR, SystemIO, 0xB2, 2)
            Field (APMR, ByteAcc, NoLock, Preserve) { SMIC, 8, SMID, 8 }
            Method (_DSM, 4, Serialized)
            {
                If (LEqual (Arg0, ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4")))
                {
                    If (LEqual (Arg1, Zero))
                    {
                        If (LEqual (Arg2, Zero)) { Return (Buffer (One) { 3 }) }
                        If (LEqual (Arg2, One))
                        {
                            // Share the APM port lock with the diagnostic SSDT.
                            Acquire (\_SB.VPRB.MBXL, 0xFFFF)
                            Store (0x55, SMID)
                            Store (0xE7, SMIC)
                            Store (SMID, Local0)
                            Release (\_SB.VPRB.MBXL)
                            If (LEqual (Local0, 0xA5)) { Return (Zero) }
                            Return (One)
                        }
                    }
                }
                // PPI/MOR are not advertised by this initial transport milestone.
                Return (Buffer (One) { 0 })
            }
        }
    }
}
