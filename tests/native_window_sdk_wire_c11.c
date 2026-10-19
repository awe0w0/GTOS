/* Freestanding C11 layout check for the window wire header.
 *
 * This translation unit is compiled against the exported SDK overlay, not
 * against the repository include tree, with `--target=i686-unknown-none-elf
 * -ffreestanding -nostdinc`. It must not include <stdio.h> or any other host
 * header: the point is to prove the wire header is self-contained C11 and that
 * its own `_Static_assert` block really fires for a C compiler.
 *
 * Every assertion below is a layout or numbering fact that a consumer depends
 * on, so a silent change in the header is a compile failure here rather than a
 * surprise at run time.
 */
#include <process/window_abi.h>

_Static_assert(GTOS_WINDOW_ABI_VERSION == 1U, "window ABI version");
_Static_assert(GTOS_SYS_WINDOW_CREATE == 0x4750U, "create opcode");
_Static_assert(GTOS_SYS_WINDOW_SUBMIT == 0x4751U, "submit opcode");
_Static_assert(GTOS_SYS_WINDOW_PRESENT == 0x4752U, "present opcode");
_Static_assert(GTOS_SYS_WINDOW_CLOSE == 0x4753U, "close opcode");
_Static_assert(GTOS_SYS_WINDOW_INFO == 0x4754U, "info opcode");
_Static_assert(GTOS_SYS_INPUT_READ == 0x4755U, "input read opcode");

_Static_assert(sizeof(GtosWindowCreateRequest) == GTOS_WINDOW_CREATE_REQUEST_BYTES,
               "create request bytes");
_Static_assert(sizeof(GtosWindowSubmitRequest) == GTOS_WINDOW_SUBMIT_REQUEST_BYTES,
               "submit request bytes");
_Static_assert(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES,
               "control request bytes");
_Static_assert(sizeof(GtosWindowInfoRequest) == GTOS_WINDOW_INFO_REQUEST_BYTES,
               "info request bytes");
_Static_assert(sizeof(GtosInputReadRequest) == GTOS_INPUT_READ_REQUEST_BYTES,
               "input read request bytes");
_Static_assert(sizeof(GtosWindowInfoResponse) == GTOS_WINDOW_INFO_RESPONSE_BYTES,
               "info response bytes");
_Static_assert(sizeof(GtosInputEvent) == GTOS_INPUT_EVENT_BYTES, "event bytes");

_Static_assert(GTOS_WINDOW_MAX_WIDTH == 32U, "max width");
_Static_assert(GTOS_WINDOW_MAX_HEIGHT == 32U, "max height");
_Static_assert(GTOS_WINDOW_MAX_BYTES == 4096U, "max bytes");
_Static_assert(GTOS_WINDOW_WRITE_LIMIT == 256U, "write limit");
_Static_assert(GTOS_WINDOW_MAX_OBJECTS == 4U, "max objects");
_Static_assert(GTOS_WINDOW_MAX_PER_OWNER == 1U, "max per owner");
_Static_assert(GTOS_INPUT_READ_LIMIT == 32U, "input read limit");
_Static_assert(GTOS_INPUT_EVENT_BYTES == 16U, "input event bytes");

/* The error numbering is part of the interface: a consumer that tests for
 * BAD_HANDLE must get the kernel's number, not a local guess. */
_Static_assert(GTOS_WINDOW_ERR_TOO_LARGE == -7, "too large");
_Static_assert(GTOS_WINDOW_ERR_BAD_HANDLE == -9, "bad handle");
_Static_assert(GTOS_WINDOW_ERR_BAD_ADDRESS == -14, "bad address");
_Static_assert(GTOS_WINDOW_ERR_LIMIT == -24, "limit");
_Static_assert(GTOS_WINDOW_ERR_RANGE == -34, "range");
_Static_assert(GTOS_WINDOW_ERR_UNSUPPORTED_VERSION == -38, "unsupported version");
_Static_assert(GTOS_WINDOW_ERR_OVERFLOW == -75, "overflow");
_Static_assert(GTOS_WINDOW_ERR_BAD_PIXELS == -84, "bad pixels");

/* A single exported symbol keeps the object non-empty, so the compiler cannot
 * drop the whole unit before the assertions are evaluated. */
int gtos_window_sdk_wire_c11_check(void) {
    return (int)GTOS_WINDOW_INFO_RESPONSE_BYTES + (int)GTOS_INPUT_EVENT_BYTES;
}
