#ifndef VTPM_PORT_COMPAT_H_
#define VTPM_PORT_COMPAT_H_

// Use EDK II's existing OpenSSL CRT shim, never the host operating system CRT.
#include <CrtLibSupport.h>
#include <stdint.h>
#include <openssl/opensslconf.h>
typedef INTN ptrdiff_t;

// Upstream hash descriptors take the address of memcpy. EDK's function-like
// CRT macro cannot be used as a function pointer; provide a matching wrapper.
VOID *VtpmCopyMem (VOID *Destination, CONST VOID *Source, UINTN Size);
#undef memcpy
#define memcpy VtpmCopyMem

// This EDK II backend omits EC. The upstream math adapter still declares an
// opaque curve holder when ALG_ECC is disabled. No EC functions are called.
#ifdef OPENSSL_NO_EC
typedef struct ec_group_st EC_GROUP;
#define MAX_ECC_KEY_BITS 0
#define MAX_ECC_KEY_BYTES 0
#define ECC_CURVE_COUNT 0
#endif

// Microsoft uses DEBUG as a numeric configuration switch. Keep this private
// to TPM translation units; EDK II modules retain their DebugLib DEBUG macro.
#undef DEBUG
#define DEBUG 0

#endif
