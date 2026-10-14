// Test-only wrappers supply the exact four _DSM arguments, including an empty package.
DefinitionBlock ("", "SSDT", 2, "VTPM2 ", "WINTTEST", 1)
{
    External (\_SB.VTPM._DSM, MethodObj)
    Method (WQRY, 0) {
        Return (\_SB.VTPM._DSM (ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4"), Zero, Zero, Package () {}))
    }
    Method (WSTR, 0) {
        Return (\_SB.VTPM._DSM (ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4"), Zero, One, Package () {}))
    }
    Method (WREV, 0) {
        Return (\_SB.VTPM._DSM (ToUUID ("6bbf6cab-5463-4714-b7cd-f0203c0368d4"), One, Zero, Package () {}))
    }
}
