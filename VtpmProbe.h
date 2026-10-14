#ifndef VTPM_PROBE_H_
#define VTPM_PROBE_H_

#include <Uefi.h>

// Private diagnostic protocol; not a TPM command interface.
#define VTPM_PROBE_GUID \
  { 0x6f273c4e, 0x50c8, 0x4b0a, { 0x9e, 0xab, 0x31, 0x73, 0x62, 0xd4, 0x15, 0xf1 } }
#define VTPM_PROBE_SIGNATURE SIGNATURE_32 ('V', 'T', 'P', 'M')
#define VTPM_PROBE_VERSION   1
#define VTPM_PROBE_PING      1
#define VTPM_PROBE_REGISTER_BUFFER 2
#define VTPM_PROBE_AML_ADDRESS_MARKER 0xF0E1D2C3B4A59687ULL
#define VTPM_PACKET_READY    1
#define VTPM_PACKET_DONE     2
#define VTPM_PACKET_PING     SIGNATURE_32 ('P', 'I', 'N', 'G')
#define VTPM_PACKET_PONG     SIGNATURE_32 ('P', 'O', 'N', 'G')
#define VTPM_PACKET_CAPACITY 64
#define VTPM_SHARED_PAGES 8
#define VTPM_SHARED_SIZE (VTPM_SHARED_PAGES * EFI_PAGE_SIZE)
#define VTPM_FULL_OFFSET 0x1000
#define VTPM_FULL_SIGNATURE SIGNATURE_32 ('V', 'T', 'F', '2')
#define VTPM_FULL_VERSION 1
#define VTPM_FULL_CAPACITY 4096
#define VTPM_PACKET_BUSY 3

// QEMU q35 diagnostic doorbell. These are not portable PC chipset settings.
#define VTPM_PROBE_APM_COMMAND_PORT  0xB2
#define VTPM_PROBE_APM_DATA_PORT     0xB3
#define VTPM_PROBE_SMI_COMMAND       0xE7
#define VTPM_PROBE_SMI_REQUEST       0x51
#define VTPM_PROBE_SMI_ACK           0xA5

typedef struct {
  UINT32 Signature;
  UINT32 Version;
  UINT32 Operation;
  UINT32 Sequence;
  UINT32 Result;
  UINT32 Calls;
  UINT64 SharedAddress;
} VTPM_PROBE_MESSAGE;

// Fixed offsets are shared with VtpmRuntimeProbe.asl. Diagnostic wire format.
typedef struct {
  UINT32 Signature;         // 00
  UINT32 Version;           // 04
  UINT32 State;             // 08
  UINT32 Sequence;          // 0c
  UINT32 RequestSize;       // 10
  UINT32 ResponseCapacity;  // 14
  UINT32 ResponseSize;      // 18
  UINT32 Result;            // 1c
  UINT32 ResponseSequence;  // 20
  UINT32 Calls;             // 24
  UINT8  Request[VTPM_PACKET_CAPACITY];  // 28
  UINT8  Response[VTPM_PACKET_CAPACITY]; // 68
} VTPM_PROBE_PACKET;

typedef CHAR8 VTPM_PACKET_LAYOUT_CHECK[
  (sizeof (VTPM_PROBE_PACKET) == 0xA8 &&
   OFFSET_OF (VTPM_PROBE_PACKET, Request) == 0x28 &&
   OFFSET_OF (VTPM_PROBE_PACKET, Response) == 0x68) ? 1 : -1];

// Full diagnostic transport, separate from the proven ACPI PING/64-byte channel.
typedef struct {
  UINT32 Signature;
  UINT32 Version;
  UINT32 State;
  UINT32 Sequence;
  UINT32 RequestSize;
  UINT32 ResponseCapacity;
  UINT32 ResponseSize;
  UINT32 Result;
  UINT32 ResponseSequence;
  UINT32 Calls;
  UINT8 Request[VTPM_FULL_CAPACITY];
  UINT8 Response[VTPM_FULL_CAPACITY];
} VTPM_FULL_PACKET;
typedef CHAR8 VTPM_FULL_LAYOUT_CHECK[
  (sizeof (VTPM_FULL_PACKET) == 0x2028 &&
   OFFSET_OF (VTPM_FULL_PACKET, Request) == 0x28 &&
   OFFSET_OF (VTPM_FULL_PACKET, Response) == 0x1028 &&
   VTPM_FULL_OFFSET + sizeof (VTPM_FULL_PACKET) <= VTPM_SHARED_SIZE) ? 1 : -1];
#endif
