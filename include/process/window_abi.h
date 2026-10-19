#ifndef GTOS_PROCESS_WINDOW_ABI_H
#define GTOS_PROCESS_WINDOW_ABI_H

// Additive, separately versioned extension of GTOS i386 native ABI 1.
// int 0x80: EAX=call, EBX=user request, ECX=exact request bytes.
// EAX=result; every other general and segment register is preserved.
// Positive handles never wrap and never revive; negative results are errors.
#define GTOS_WINDOW_ABI_VERSION   1U
#define GTOS_SYS_WINDOW_CREATE    0x4750U
#define GTOS_SYS_WINDOW_SUBMIT    0x4751U
#define GTOS_SYS_WINDOW_PRESENT   0x4752U
#define GTOS_SYS_WINDOW_CLOSE     0x4753U
#define GTOS_SYS_WINDOW_INFO      0x4754U
#define GTOS_SYS_INPUT_READ       0x4755U

#define GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED 1U
#define GTOS_WINDOW_MAX_WIDTH      32U
#define GTOS_WINDOW_MAX_HEIGHT     32U
#define GTOS_WINDOW_MAX_BYTES      4096U
#define GTOS_WINDOW_WRITE_LIMIT    256U
#define GTOS_WINDOW_MAX_OBJECTS    4U
#define GTOS_WINDOW_MAX_PER_OWNER  1U
#define GTOS_INPUT_STORAGE_ENTRIES 128U
#define GTOS_INPUT_NORMAL_ENTRIES  126U
#define GTOS_INPUT_READ_LIMIT      32U
#define GTOS_INPUT_EVENT_BYTES     16U
#define GTOS_INPUT_READ_SCRATCH    512U

#define GTOS_WINDOW_CREATE_REQUEST_BYTES   20U
#define GTOS_WINDOW_SUBMIT_REQUEST_BYTES   20U
#define GTOS_WINDOW_CONTROL_REQUEST_BYTES   8U
#define GTOS_WINDOW_INFO_REQUEST_BYTES     12U
#define GTOS_INPUT_READ_REQUEST_BYTES      16U
#define GTOS_WINDOW_INFO_RESPONSE_BYTES    40U

// WINDOW_INFO display enum: derived from progress/generation, never an
// operation condition.
#define GTOS_WINDOW_SHOW_EMPTY      0U
#define GTOS_WINDOW_SHOW_DRAFTING   1U
#define GTOS_WINDOW_SHOW_PUBLISHED  2U

#define GTOS_WINDOW_LIVE            0U
#define GTOS_WINDOW_CLOSING         1U
#define GTOS_WINDOW_DEAD            2U

#define GTOS_INPUT_KEY_DOWN      1U
#define GTOS_INPUT_KEY_UP        2U
#define GTOS_INPUT_POINTER_DOWN  3U
#define GTOS_INPUT_POINTER_UP    4U
#define GTOS_INPUT_POINTER_MOVE  5U
#define GTOS_INPUT_CLOSE         6U
#define GTOS_INPUT_RESET         7U

#define GTOS_WINDOW_ERR_TOO_LARGE            (-7)
#define GTOS_WINDOW_ERR_BAD_HANDLE           (-9)
#define GTOS_WINDOW_ERR_BAD_ADDRESS         (-14)
#define GTOS_WINDOW_ERR_BAD_STATE           (-22)
#define GTOS_WINDOW_ERR_BAD_SIZE            (-22)
#define GTOS_WINDOW_ERR_LIMIT               (-24)
#define GTOS_WINDOW_ERR_RANGE               (-34)
#define GTOS_WINDOW_ERR_UNSUPPORTED_VERSION (-38)
#define GTOS_WINDOW_ERR_UNSUPPORTED_FORMAT  (-38)
#define GTOS_WINDOW_ERR_OVERFLOW            (-75)
#define GTOS_WINDOW_ERR_BAD_PIXELS          (-84)
// Handle exhaustion is distinct from slot/owner capacity (-24).
#define GTOS_WINDOW_ERR_HANDLE_EXHAUSTED    (-75)

typedef struct GtosWindowCreateRequest {
    unsigned version, width, height, stride, format;
} GtosWindowCreateRequest;

typedef struct GtosWindowSubmitRequest {
    unsigned version, handle, offset, source, length;
} GtosWindowSubmitRequest;

typedef struct GtosWindowControlRequest {
    unsigned version, handle;
} GtosWindowControlRequest;

typedef struct GtosWindowInfoRequest {
    unsigned version, handle, result_va;
} GtosWindowInfoRequest;

typedef struct GtosInputReadRequest {
    unsigned version, handle, count, destination;
} GtosInputReadRequest;

// WINDOW_INFO response. show is derived; lifecycle decides what is allowed.
typedef struct GtosWindowInfoResponse {
    unsigned handle, lifecycle, show, width, height;
    unsigned frame_generation, progress, total, queue_depth, dropped;
} GtosWindowInfoResponse;

// One fixed-width input record. x/y are signed client-content relative.
typedef struct GtosInputEvent {
    unsigned type, code;
    int x, y;
} GtosInputEvent;

#if defined(__cplusplus)
static_assert(sizeof(unsigned) == 4, "Window ABI requires 32-bit unsigned");
static_assert(sizeof(int) == 4, "Window ABI requires 32-bit int");
static_assert(sizeof(GtosWindowCreateRequest) == GTOS_WINDOW_CREATE_REQUEST_BYTES, "Window create ABI");
static_assert(sizeof(GtosWindowSubmitRequest) == GTOS_WINDOW_SUBMIT_REQUEST_BYTES, "Window submit ABI");
static_assert(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES, "Window control ABI");
static_assert(sizeof(GtosWindowInfoRequest) == GTOS_WINDOW_INFO_REQUEST_BYTES, "Window info ABI");
static_assert(sizeof(GtosInputReadRequest) == GTOS_INPUT_READ_REQUEST_BYTES, "Input read ABI");
static_assert(sizeof(GtosWindowInfoResponse) == GTOS_WINDOW_INFO_RESPONSE_BYTES, "Window info response ABI");
static_assert(sizeof(GtosInputEvent) == GTOS_INPUT_EVENT_BYTES, "Input event ABI");
static_assert(__builtin_offsetof(GtosInputEvent, x) == 8, "Input event x offset");
static_assert(__builtin_offsetof(GtosInputEvent, y) == 12, "Input event y offset");
static_assert(__builtin_offsetof(GtosWindowInfoResponse, frame_generation) == 20, "Info generation offset");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(unsigned) == 4, "Window ABI requires 32-bit unsigned");
_Static_assert(sizeof(int) == 4, "Window ABI requires 32-bit int");
_Static_assert(sizeof(GtosWindowCreateRequest) == GTOS_WINDOW_CREATE_REQUEST_BYTES, "Window create ABI");
_Static_assert(sizeof(GtosWindowSubmitRequest) == GTOS_WINDOW_SUBMIT_REQUEST_BYTES, "Window submit ABI");
_Static_assert(sizeof(GtosWindowControlRequest) == GTOS_WINDOW_CONTROL_REQUEST_BYTES, "Window control ABI");
_Static_assert(sizeof(GtosWindowInfoRequest) == GTOS_WINDOW_INFO_REQUEST_BYTES, "Window info ABI");
_Static_assert(sizeof(GtosInputReadRequest) == GTOS_INPUT_READ_REQUEST_BYTES, "Input read ABI");
_Static_assert(sizeof(GtosWindowInfoResponse) == GTOS_WINDOW_INFO_RESPONSE_BYTES, "Window info response ABI");
_Static_assert(sizeof(GtosInputEvent) == GTOS_INPUT_EVENT_BYTES, "Input event ABI");
_Static_assert(__builtin_offsetof(GtosInputEvent, x) == 8, "Input event x offset");
_Static_assert(__builtin_offsetof(GtosInputEvent, y) == 12, "Input event y offset");
#endif
#endif
