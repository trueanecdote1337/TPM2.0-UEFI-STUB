DefinitionBlock ("SsdtVtpm.aml", "SSDT", 2, "VTPM2 ", "VTPMENUM", 0x00000001)
{
    Scope (\_SB)
    {
        Device (VTP0)
        {
            Name (_HID, "MSFT0101")
            Name (_STR, Unicode ("Trusted Platform Module 2.0"))
            Name (_UID, One)
            Name (_STA, 0x0F)

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFED40000, 0x00005000)
            })
        }
    }
}