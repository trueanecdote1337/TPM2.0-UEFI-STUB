#ifndef VTPM_CORE_RUN_H_
#define VTPM_CORE_RUN_H_
#include <Base.h>
#include <Library/BaseLib.h>
#define VTPM_RUN_OK          0u
#define VTPM_RUN_ARGUMENT    1u
#define VTPM_RUN_NOT_READY   2u
#define VTPM_RUN_FAILURE     3u
#define VTPM_RUN_NV          4u
#define VTPM_RUN_MANUFACTURE 5u
UINT32 EFIAPI VtpmCoreCommandCapacity(VOID);
UINT32 EFIAPI VtpmCoreResponseCapacity(VOID);
// Initialization is once per ephemeral SMM instance, never once per command.
UINT32 EFIAPI VtpmCoreInitialize(VOID);
// Trusted private SMM buffers only. Transport must validate/copy shared buffers.
// Require full response capacity before execution, preventing lost stateful replies.
UINT32 EFIAPI VtpmCoreSubmit(UINT32 Size, CONST UINT8 *Request,
                           UINT32 Capacity, UINT8 *Response, UINT32 *ResponseSize);
// Temporary boundary for pre-initialization probes. Clear before leaving entry.
VOID EFIAPI VtpmCoreProbeBoundary(BASE_LIBRARY_JUMP_BUFFER *Boundary);
#define VTPM_COMMAND_TEST_INIT       BIT0
#define VTPM_COMMAND_TEST_STARTUP    BIT1
#define VTPM_COMMAND_TEST_CAPABILITY BIT2
#define VTPM_COMMAND_TEST_SELFTEST   BIT3
#define VTPM_COMMAND_TEST_RANDOM     BIT4
#define VTPM_COMMAND_TEST_ERRORS     BIT5
#define VTPM_COMMAND_TEST_ALL        (BIT0|BIT1|BIT2|BIT3|BIT4|BIT5)
UINT32 EFIAPI VtpmCommandSmokeTest(UINT32 *Stage);
#endif
