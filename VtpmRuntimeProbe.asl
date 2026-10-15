// QEMU q35 only. Diagnostic device, deliberately not MSFT0101 / a TPM.
DefinitionBlock ("VtpmRuntimeProbe.aml", "SSDT", 2, "VTPM2 ", "VTPMPROB", 1)
{
    Scope (\_SB)
    {
        Device (VPRB)
        {
            Name (_HID, "VTP0001")
            Name (_UID, One)
            Name (_STA, 0x0F)
            Name (_STR, Unicode ("Firmware TPM SMM transport probe"))

            // DXE replaces this unique QWord marker with allocated reserved shared memory.
            OperationRegion (MBXR, SystemMemory, 0xF0E1D2C3B4A59687, 0xA8)
            Field (MBXR, DWordAcc, NoLock, Preserve)
            {
                MSIG, 32, MVER, 32, MSTA, 32, MSEQ, 32,
                RLEN, 32, OCAP, 32, OLEN, 32, RSLT, 32,
                OSEQ, 32, NCAL, 32, REQD, 32,
                Offset (0x68), RSPD, 32
            }

            // SystemMemory uses byte accesses; a 512-bit FieldUnit is still a Buffer.
            Field (MBXR, ByteAcc, NoLock, Preserve)
            {
                Offset (0x28), TREQ, 512,
                Offset (0x68), TRES, 512
            }

            OperationRegion (APMR, SystemIO, 0xB2, 2)
            Field (APMR, ByteAcc, NoLock, Preserve)
            {
                SMIC, 8,
                SMID, 8
            }

            // Serialize both diagnostic methods over the shared mailbox.
            Mutex (MBXL, 0)
            Method (VTST, 0, Serialized)
            {
                Acquire (MBXL, 0xFFFF)
                Store (VTPG (), Local0)
                Release (MBXL)
                Return (Local0)
            }
            Method (TCMD, 3, Serialized)
            {
                Acquire (MBXL, 0xFFFF)
                Store (TPCX (Arg0, Arg1, Arg2), Local0)
                Release (MBXL)
                Return (Local0)
            }

            // Synchronous SMI: the read occurs after the handler has returned.
            Method (VTPG, 0, Serialized)
            {
                Store (0x4D505456, MSIG) // VTPM, little-endian
                Store (One, MVER)
                Increment (MSEQ)
                Store (4, RLEN)
                Store (64, OCAP)
                Store (Zero, OLEN)
                Store (Ones, RSLT)
                Store (Zero, RSPD)
                Store (0x474E4950, REQD) // PING
                Store (One, MSTA)       // Request complete: publish last
                Store (0x51, SMID)
                Store (0xE7, SMIC)
                If (LAnd (LEqual (SMID, 0xA5), LEqual (MSTA, 2)))
                {
                    If (LAnd (LEqual (RSLT, Zero), LEqual (OSEQ, MSEQ)))
                    {
                        If (LAnd (LEqual (OLEN, 4), LEqual (RSPD, 0x474E4F50)))
                        { Return (Zero) } // PONG
                    }
                }
                Return (One)
            }

            // Bounded TPM requests to the SAME core that passed boot tests.
            Method (TPCX, 3, Serialized)
            {
                Store (0x4D505456, MSIG)
                Store (One, MVER)
                Increment (MSEQ)
                Store (Arg0, TREQ)
                Store (Arg1, RLEN)
                Store (64, OCAP)
                Store (Zero, OLEN)
                Store (Ones, RSLT)
                Store (One, MSTA)
                Store (0x51, SMID)
                Store (0xE7, SMIC)
                If (LOr (LNotEqual (SMID, 0xA5), LNotEqual (MSTA, 2))) { Return (One) }
                If (LOr (LNotEqual (RSLT, Zero), LNotEqual (OSEQ, MSEQ))) { Return (One) }
                If (LNotEqual (OLEN, Arg2)) { Return (One) }
                Store (TRES, Local0)
                If (LOr (LNotEqual (DerefOf (Index (Local0, 0)), 0x80),
                         LNotEqual (DerefOf (Index (Local0, 1)), One))) { Return (One) }
                Store (2, Local1)
                While (LLess (Local1, 10))
                {
                    If (LEqual (Local1, 5))
                    {
                        If (LNotEqual (DerefOf (Index (Local0, Local1)), Arg2)) { Return (One) }
                    }
                    Else
                    {
                        If (LNotEqual (DerefOf (Index (Local0, Local1)), Zero)) { Return (One) }
                    }
                    Increment (Local1)
                }
                If (LEqual (Arg2, 27))
                {
                    If (LNotEqual (DerefOf (Index (Local0, 23)), 0x56)) { Return (One) }
                    If (LNotEqual (DerefOf (Index (Local0, 24)), 0x54)) { Return (One) }
                    If (LNotEqual (DerefOf (Index (Local0, 25)), 0x50)) { Return (One) }
                    If (LNotEqual (DerefOf (Index (Local0, 26)), 0x4D)) { Return (One) }
                }
                Else
                {
                    If (LNotEqual (DerefOf (Index (Local0, 10)), Zero)) { Return (One) }
                    If (LNotEqual (DerefOf (Index (Local0, 11)), 32)) { Return (One) }
                }
                Return (Zero)
            }

            Method (VTCM, 0, Serialized)
            {
                If (TCMD (Buffer (22) {0x80,1,0,0,0,22,0,0,1,0x7A,
                                      0,0,0,6,0,0,1,5,0,0,0,1}, 22, 27)) { Return (One) }
                Return (TCMD (Buffer (12) {0x80,1,0,0,0,12,0,0,1,0x7B,0,32}, 12, 44))
            }

            // The OS ACPI interpreter invokes this during device enumeration.
            Method (_INI, 0, NotSerialized)
            {
                VTST ()
                VTCM ()
            }

            // Private diagnostic DSM, not the TCG TPM start-method DSM.
            Method (_DSM, 4, Serialized)
            {
                If (LNotEqual (Arg0, ToUUID ("6f273c4e-50c8-4b0a-9eab-317362d415f1")))
                { Return (Buffer (One) { 0 }) }
                If (LNotEqual (Arg1, One))
                { Return (Buffer (One) { 0 }) }
                If (LEqual (Arg2, Zero))
                { Return (Buffer (One) { 0x07 }) }
                If (LEqual (Arg2, One)) { Return (VTST ()) }
                If (LEqual (Arg2, 2)) { Return (VTCM ()) }
                Return (Buffer (One) { 0 })
            }
        }
    }
}
