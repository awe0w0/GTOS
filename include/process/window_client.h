#ifndef GTOS_PROCESS_WINDOW_CLIENT_H
#define GTOS_PROCESS_WINDOW_CLIENT_H

// Thin user-space wrapper for the six window wire calls in
// <process/window_abi.h>. This is a client of the ABI, not part of it: it adds
// no opcode, no field, no limit and no policy. Each function builds nothing at
// all - it passes the caller's request pointer and the caller's exact byte count
// straight through - so the kernel remains the only thing that validates a
// request.
//
// What this header deliberately does NOT do:
//   * no pre-validation. A wrong byte count, a null request or a stale handle
//     must reach the kernel and be refused there; a wrapper that screened them
//     in user space would turn a kernel negative test into a user-space test.
//   * no result rewriting. CREATE's positive handle stays a positive handle, a
//     failure stays the kernel's negative errno, SUBMIT's result stays a byte
//     count, INPUT_READ's result stays an event count. None of them becomes a
//     bool, and none is narrowed to 8 or 16 bits.
//   * no ownership. Nothing here closes a handle implicitly, and a second CLOSE
//     is the kernel's BAD_HANDLE, not a silent success.
//   * no transport, no allocation, no exceptions, no threads, no waiting.
//
// Only i386 GTOS is supported. Building this for a host operating system is a
// compile-time error, because `int $0x80` would then mean a different syscall
// table entirely.

#include <process/window_abi.h>

#if !defined(__cplusplus)
#error "window_client.h is a C++ user-space wrapper; include window_abi.h for the wire layout"
#endif
#if !defined(__i386__)
#error "window_client.h is a GTOS i386 user-space wrapper; only __i386__ is supported"
#endif
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
#error "window_client.h must not be compiled for a host OS: int $0x80 is GTOS-only"
#endif

namespace gtos {
namespace process {

// The ABI 1 convention: EAX=call, EBX=request, ECX=exact bytes, EAX=result.
// Memory and the condition codes are clobbered; every other register is
// preserved by the kernel, so the constraint list stays this narrow.
static inline int WindowCall(unsigned operation, unsigned request, unsigned bytes) {
    int result;
    __asm__ __volatile__("int $0x80"
                         : "=a"(result)
                         : "a"(operation), "b"(request), "c"(bytes)
                         : "memory", "cc");
    return result;
}

// CREATE. Returns the new positive handle, or a negative error. A handle is
// never zero on success and never wraps, so the caller must test `< 0`, not
// `== 0`, to detect failure.
static inline int WindowCreate(const GtosWindowCreateRequest *request,
                               unsigned bytes = GTOS_WINDOW_CREATE_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_WINDOW_CREATE, (unsigned)request, bytes);
}

// SUBMIT. Returns the number of bytes actually accepted, or a negative error.
// This ABI accepts the entire requested chunk on success; it does not
// advertise a partial-success result. The returned value remains a byte count.
static inline int WindowSubmit(const GtosWindowSubmitRequest *request,
                               unsigned bytes = GTOS_WINDOW_SUBMIT_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_WINDOW_SUBMIT, (unsigned)request, bytes);
}

// PRESENT. Returns 0 or a negative error. Not folded into a bool: the kernel's
// errno is the only useful diagnostic and it is handed back unchanged.
static inline int WindowPresent(const GtosWindowControlRequest *request,
                                unsigned bytes = GTOS_WINDOW_CONTROL_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_WINDOW_PRESENT, (unsigned)request, bytes);
}

// CLOSE. Destroys the object. Returns 0 or a negative error; a second CLOSE of
// the same handle is BAD_HANDLE. This is not the desktop's idempotent
// RequestClose, and the two must not be described as the same operation.
static inline int WindowClose(const GtosWindowControlRequest *request,
                              unsigned bytes = GTOS_WINDOW_CONTROL_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_WINDOW_CLOSE, (unsigned)request, bytes);
}

// INFO. `request->result_va` is a 32-bit user address that receives a
// GtosWindowInfoResponse. Returns 0 or a negative error.
static inline int WindowInfo(const GtosWindowInfoRequest *request,
                             unsigned bytes = GTOS_WINDOW_INFO_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_WINDOW_INFO, (unsigned)request, bytes);
}

// INPUT_READ. `request->destination` is a 32-bit user address and
// `request->count` is a record count, not a byte count. Returns the number of
// records copied - 0 for an empty queue, which copies nothing at all - or a
// negative error. RESET and CLOSE arrive here as ordinary records; they are not
// delivered as some separate synchronous event.
static inline int WindowInputRead(const GtosInputReadRequest *request,
                                  unsigned bytes = GTOS_INPUT_READ_REQUEST_BYTES) {
    return WindowCall(GTOS_SYS_INPUT_READ, (unsigned)request, bytes);
}

}  // namespace process
}  // namespace gtos

#endif
