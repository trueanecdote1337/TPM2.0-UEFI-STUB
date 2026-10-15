# Third-party notices

The root [MIT license](LICENSE) applies to original project code and documentation.
It does not replace the licenses of imported files or their derived ports.
Retain the copyright notices in each source file.

| Component | Files in this repository | License text |
| --- | --- | --- |
| Microsoft / TCG TPM 2.0 reference implementation | `TpmCoreLib/Vendor/`, derived configuration and reference-derived files under `TpmCoreLib/Port/` | [Microsoft/TCG BSD terms](licenses/Microsoft-TCG.txt); original copy also at `TpmCoreLib/LICENSE.Microsoft-TCG` |
| EDK II | `TpmCoreLib/EdkHeaders/Crt/`, `VtpmSmmCpuFeaturesLib/`, EDK-derived portions | [EDK II BSD terms](licenses/EDK-II.txt) and notices retained in the files |
| OpenSSL 1.1.0h | `TpmCoreLib/EdkHeaders/OpenSsl/` | [OpenSSL / SSLeay terms](licenses/OpenSSL-1.1.0h.txt) and individual header notices |

The project links against the selected EDK II checkout's CryptoPkg/OpenSSL libraries;
it does not ship a compiled OpenSSL library. A firmware binary also contains other
EDK II components. Their notices remain part of any binary redistribution.

The exact original TPM commit was not recorded during the initial snapshot.
[`UPSTREAM_SHA256.json`](TpmCoreLib/UPSTREAM_SHA256.json) records all 487 imported
TPM files; [`EDK_HEADERS_SHA256.json`](TpmCoreLib/EDK_HEADERS_SHA256.json) records
106 copied EDK/OpenSSL headers. `scripts/verify-sources.py` verifies both snapshots.
No claim of an exact upstream TPM revision is made.
