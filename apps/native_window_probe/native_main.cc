// Real freestanding GTOS i386 window consumer.
//
// It keeps the qualified resource/PNG path of apps/png_surface_probe (read the
// immutable public PNG, decode it with the unchanged Wuffs/Skia objects) and
// replaces the old surface presentation with the native window ABI:
// CREATE, SUBMIT, PRESENT, INFO, INPUT_READ and CLOSE.
//
// Everything here runs as an ordinary CPL3 process: the request descriptors and
// the pixel source are real user pages, and every byte crosses the kernel's
// checked copy. Nothing is a host simulation.
#include <process/abi.h>
#include <process/resource_abi.h>
#include <process/window_abi.h>
#include <process/window_client.h>
#include <process/vm_abi.h>
#include "png_pixel.h"
#include "resource_oracles.h"
#include <stddef.h>
extern "C" void *memset(void *, int, size_t);
extern "C" int resource_probe_registers(unsigned, unsigned, unsigned);
// Linker-derived exclusive end of this process's RW pages. This diagnostic
// symbol describes our own ELF and never appears in the window ABI.
extern "C" unsigned char resource_oracles_rw_limit;
#if !defined(__i386__) || defined(__linux__) || defined(_WIN32)
#error A real freestanding GTOS i386 application is required
#endif
static_assert(sizeof(void *) == 4, "GTOS i386 ABI1");
static_assert(sizeof(GtosWindowCreateRequest) == 20, "window CREATE v1");
static_assert(sizeof(GtosWindowSubmitRequest) == 20, "window SUBMIT v1");
static_assert(sizeof(GtosWindowControlRequest) == 8, "window control v1");
static_assert(sizeof(GtosWindowInfoRequest) == 12, "window INFO v1");
static_assert(sizeof(GtosInputReadRequest) == 16, "input READ v1");
static_assert(sizeof(GtosWindowInfoResponse) == 40, "window INFO response v1");
static_assert(sizeof(GtosInputEvent) == 16, "input event v1");
static_assert(sizeof(GtosVmReserveRequest) == GTOS_VM_RESERVE_REQUEST_BYTES, "vm reserve v1");
static_assert(sizeof(GtosVmRangeRequest) == GTOS_VM_RANGE_REQUEST_BYTES, "vm range v1");
static_assert(sizeof(GtosVmControlRequest) == GTOS_VM_CONTROL_REQUEST_BYTES, "vm control v1");

alignas(8) static unsigned char context[65536];
static unsigned char input[2048], work[4096], bgra[1024], rgba[1024];
alignas(4096) static unsigned char boundary[8192];
// Input records land here through the real INPUT_READ copy.
static unsigned char inbox[GTOS_INPUT_READ_LIMIT * GTOS_INPUT_EVENT_BYTES];
static unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
// Request and output share these bytes so an overlap case is a real overlap.
alignas(8) static unsigned char shared[32];
// Every byte past the delivered records must stay at the sentinel value.
alignas(8) static unsigned char sentinelled[GTOS_INPUT_READ_LIMIT * GTOS_INPUT_EVENT_BYTES + 32];
// INFO lands here; every query in this process uses this one owned buffer.
alignas(8) static GtosWindowInfoResponse infoOut;
static volatile unsigned initialized = 0x47544f53U;

// Required-stage mask. The final PASS is gated on this, not on reaching the end
// of the function, so a stage that never runs can never be silently skipped.
#define REQ_INPUT_COPY 1U
#define REQ_RESET_COPY 2U
#define REQ_CLOSE_COPY 4U
#define REQ_ALL (REQ_INPUT_COPY | REQ_RESET_COPY | REQ_CLOSE_COPY)
static volatile unsigned requiredMask = 0;
// Real records the desktop delivered, counted per type.
static unsigned inputSeen, inputDown, inputUp, inputMoves, inputKeys, inputReset;

static int call(unsigned n, unsigned a = 0, unsigned b = 0) {
    int result;
    asm volatile("int $0x80" : "=a"(result) : "a"(n), "b"(a), "c"(b) : "memory", "cc");
    return result;
}
extern "C" void gtos_skia_assert_failure(void) {
    call(GTOS_SYS_EXIT, 0x5253ffffU);
    asm volatile("ud2");
    __builtin_unreachable();
}
#define CHECK(c, id) do { if (!(c)) return 0x52000000U | (id); } while (false)
static void say(const char *text, unsigned n) { call(GTOS_SYS_WRITE, (unsigned)text, n); }
#define SAY(literal) say(literal, (unsigned)(sizeof(literal) - 1))
// Decimal, so a stage line can report how many cases really executed.
static void sayNumber(unsigned value) {
    char digits[10];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    char out[10];
    for (unsigned i = 0; i < count; ++i) out[i] = digits[count - 1 - i];
    say(out, count);
}
// A stage is only "done" when it reports the number of cases it executed, and
// the harness waits for that line before it drives the next piece of input.
#define STAGE(name, cases) do { \
    SAY("GTOS NATIVE WINDOW STAGE " name " V1 "); \
    sayNumber(cases); \
    SAY("\n"); \
} while (false)
static bool equal(const unsigned char *a, const unsigned char *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (a[i] != b[i]) return false;
    return true;
}
static bool untouched(const unsigned char *p, unsigned n, unsigned char v) {
    for (unsigned i = 0; i < n; ++i)
        if (p[i] != v) return false;
    return true;
}
static uint32_t crc32(const unsigned char *p, unsigned n) {
    uint32_t crc = 0xffffffffU;
    for (unsigned i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
    }
    return ~crc;
}

// ---- window syscall wrappers -------------------------------------------------
// Normal window calls go through the exported SDK wrapper in
// <process/window_client.h>. That header adds no validation: it passes the
// caller's request pointer and byte count straight to `int $0x80`, so a request
// the kernel should refuse is still refused by the kernel.
//
// The deliberately malformed requests - bad version, bad request size, bad
// destination pointer - must NOT travel through any wrapper, because a wrapper
// that ever grew a pre-check would quietly turn a kernel negative test into a
// user-space test that proves nothing. Those three classes use the `raw*`
// helpers below, which are the bare `int $0x80` and nothing else. Every other
// negative in this file (BAD_STATE, BAD_HANDLE, BAD_PIXELS, RANGE, LIMIT,
// second CLOSE) is a well-formed request that the kernel refuses on policy, so
// it keeps using the wrapper.
static unsigned sdkCalls, rawCalls;
static int wcreate(const GtosWindowCreateRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowCreate(q, bytes);
}
static int wsubmit(const GtosWindowSubmitRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowSubmit(q, bytes);
}
static int wpresent(const GtosWindowControlRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowPresent(q, bytes);
}
static int wclose(const GtosWindowControlRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowClose(q, bytes);
}
static int winfo(const GtosWindowInfoRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowInfo(q, bytes);
}
static int iread(const GtosInputReadRequest *q, unsigned bytes = sizeof(*q)) {
    ++sdkCalls;
    return gtos::process::WindowInputRead(q, bytes);
}
// Raw path: the bare ABI call, used only by the three malformed-request classes.
static int rawcreate(const GtosWindowCreateRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_WINDOW_CREATE, (unsigned)q, bytes);
}
static int rawsubmit(const GtosWindowSubmitRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_WINDOW_SUBMIT, (unsigned)q, bytes);
}
static int rawpresent(const GtosWindowControlRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_WINDOW_PRESENT, (unsigned)q, bytes);
}
static int rawinfo(const GtosWindowInfoRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_WINDOW_INFO, (unsigned)q, bytes);
}
static int rawiread(const GtosInputReadRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_INPUT_READ, (unsigned)q, bytes);
}

// Every rejected request must leave user memory exactly as it was.
static int rejected_creates() {
    const unsigned limit = (unsigned)&resource_oracles_rw_limit;
    GtosWindowCreateRequest good = {GTOS_WINDOW_ABI_VERSION, 16, 16, 64,
                                    GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED};
    CHECK(rawcreate(&good, 19) == GTOS_WINDOW_ERR_BAD_SIZE, 301);
    CHECK(rawcreate(&good, 21) == GTOS_WINDOW_ERR_BAD_SIZE, 302);
    CHECK(call(GTOS_SYS_WINDOW_CREATE, 0, 0) == GTOS_WINDOW_ERR_BAD_SIZE, 303);
    CHECK(call(GTOS_SYS_WINDOW_CREATE, 0, sizeof(good)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 304);
    CHECK(call(GTOS_SYS_WINDOW_CREATE, 0x100000U, sizeof(good)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 305);
    CHECK(call(GTOS_SYS_WINDOW_CREATE, limit, sizeof(good)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 306);
    GtosWindowCreateRequest q = good;
    q.version = 0;
    CHECK(rawcreate(&q) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 307);
    q = good;
    q.version = 2;
    CHECK(rawcreate(&q) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 308);
    q = good;
    q.width = 0;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 309);
    q = good;
    q.height = 0;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 310);
    // An out-of-range dimension is a range error, not a size error: the ABI
    // reports the condition it actually checked.
    q = good;
    q.width = GTOS_WINDOW_MAX_WIDTH + 1;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 311);
    q = good;
    q.height = GTOS_WINDOW_MAX_HEIGHT + 1;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 312);
    // The stride must be exactly width*4, so a mismatch is a range error.
    q = good;
    q.stride = 0;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 313);
    q = good;
    q.stride = 0xffffffffU;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_RANGE, 314);
    q = good;
    q.format = 0;
    CHECK(wcreate(&q) == GTOS_WINDOW_ERR_UNSUPPORTED_FORMAT, 315);
    // The descriptor itself may straddle two owned pages. This object is only
    // 192 bytes, so it also gives a real RANGE case for SUBMIT below.
    GtosWindowCreateRequest small = {GTOS_WINDOW_ABI_VERSION, 3, 16, 12,
                                     GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED};
    GtosWindowCreateRequest *crossing = (GtosWindowCreateRequest *)(boundary + 4096 - 8);
    *crossing = small;
    const int first = wcreate(crossing);
    CHECK(first > 0, 316);
    GtosWindowSubmitRequest oversize = {GTOS_WINDOW_ABI_VERSION, (unsigned)first, 0,
                                        (unsigned)chunk, GTOS_WINDOW_WRITE_LIMIT};
    CHECK(wsubmit(&oversize) == GTOS_WINDOW_ERR_RANGE, 323);
    oversize.length = 192;
    CHECK(wsubmit(&oversize) == 192, 324);
    // One object per owner: a second create is refused, not silently ignored.
    CHECK(wcreate(&good) == GTOS_WINDOW_ERR_LIMIT, 317);
    GtosWindowControlRequest control = {GTOS_WINDOW_ABI_VERSION, (unsigned)first};
    CHECK(wclose(&control) == 0, 318);
    // A closed handle is gone for good.
    CHECK(wclose(&control) == GTOS_WINDOW_ERR_BAD_HANDLE, 319);
    return 0;
}

// The exact 16x16 image this process decoded, published as one frame.
static unsigned char frame[RESOURCE_WIDTH * RESOURCE_HEIGHT * 4];
// A scaled copy keeps every channel <= its alpha, so the result is still a valid
// premultiplied image. Integer scaling by k/255 preserves that ordering.
static int fill(unsigned tone) {
    for (unsigned i = 0; i < sizeof(frame); ++i)
        frame[i] = (unsigned char)((rgba[i] * tone) / 255U);
    return 0;
}
static int publish(unsigned handle, unsigned tone, unsigned from) {
    fill(tone);
    GtosWindowSubmitRequest submit = {GTOS_WINDOW_ABI_VERSION, handle, 0, 0, 0};
    for (unsigned offset = from; offset < sizeof(frame); offset += GTOS_WINDOW_WRITE_LIMIT) {
        submit.offset = offset;
        submit.source = (unsigned)(frame + offset);
        submit.length = GTOS_WINDOW_WRITE_LIMIT;
        CHECK(wsubmit(&submit) == (int)GTOS_WINDOW_WRITE_LIMIT, 330);
    }
    GtosWindowControlRequest control = {GTOS_WINDOW_ABI_VERSION, handle};
    CHECK(wpresent(&control) == 0, 331);
    return 0;
}

// ---- the five-page region the copy matrix is proved against -----------------
// page 0  RW     valid single-record destination
// page 1  hole   never committed: a genuinely unmapped page
// page 2  READ   read-only destination, promoted to RW for the differential
// page 3  RW     start of the multi-record destination
// page 4  READ   read-only crossing target, pre-filled so it can be verified
static unsigned vmBase, vmHandle;
static unsigned char *vmPage(unsigned index) {
    return (unsigned char *)(vmBase + index * GTOS_VM_PAGE_BYTES);
}
static int vm_range(unsigned offset, unsigned length, unsigned protection) {
    GtosVmRangeRequest request = {GTOS_VM_ABI_VERSION, vmHandle, offset, length, protection};
    return call(GTOS_SYS_VM_SET_PERMISSIONS, (unsigned)&request, sizeof(request));
}
static int vm_setup() {
    alignas(8) static GtosVmReserveResult reserved;
    GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, 5 * GTOS_VM_PAGE_BYTES,
                                    GTOS_VM_PAGE_BYTES, 0, (unsigned)&reserved};
    CHECK(call(GTOS_SYS_VM_RESERVE, (unsigned)&reserve, sizeof(reserve)) == 0, 520);
    CHECK(reserved.version == GTOS_VM_ABI_VERSION, 521);
    CHECK(reserved.handle != 0 && reserved.handle != 0xffffffffU, 522);
    CHECK(reserved.length == 5 * GTOS_VM_PAGE_BYTES, 523);
    CHECK(reserved.page_size == GTOS_VM_PAGE_BYTES, 524);
    CHECK(reserved.base >= 0x100000U && (reserved.base & (GTOS_VM_PAGE_BYTES - 1U)) == 0, 525);
    vmBase = reserved.base;
    vmHandle = reserved.handle;
    // Page 1 is deliberately left as a hole.
    CHECK(vm_range(0, GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE) == 0, 526);
    CHECK(vm_range(2 * GTOS_VM_PAGE_BYTES, GTOS_VM_PAGE_BYTES, GTOS_VM_READ) == 0, 527);
    CHECK(vm_range(3 * GTOS_VM_PAGE_BYTES, GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE) == 0, 528);
    CHECK(vm_range(4 * GTOS_VM_PAGE_BYTES, GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE) == 0, 529);
    // Fill the crossing target while it is still writable, then seal it, so a
    // refused copy can be shown to have changed nothing at all.
    unsigned char *crossing = vmPage(4);
    for (unsigned i = 0; i < GTOS_VM_PAGE_BYTES; ++i) crossing[i] = 0xa5;
    CHECK(vm_range(4 * GTOS_VM_PAGE_BYTES, GTOS_VM_PAGE_BYTES, GTOS_VM_READ) == 0, 530);
    // A committed page is really writable and really reads back.
    unsigned char *first = vmPage(0);
    for (unsigned i = 0; i < GTOS_VM_PAGE_BYTES; ++i) first[i] = 0x3c;
    for (unsigned i = 0; i < GTOS_VM_PAGE_BYTES; ++i)
        if (first[i] != 0x3c) return 0x52000000U | 531;
    return 0;
}
static int query_info(unsigned handle, unsigned &depth, unsigned &lifecycle) {
    GtosWindowInfoRequest request = {GTOS_WINDOW_ABI_VERSION, handle, (unsigned)&infoOut};
    const int result = winfo(&request);
    if (result == 0) { depth = infoOut.queue_depth; lifecycle = infoOut.lifecycle; }
    return result;
}
// Count one delivered record and validate its shape. Every path that reads a
// record goes through here, so the totals cover the whole run and not just the
// final drain.
static int classify(const GtosInputEvent &e) {
    ++inputSeen;
    if (e.type == GTOS_INPUT_POINTER_DOWN) ++inputDown;
    else if (e.type == GTOS_INPUT_POINTER_UP) ++inputUp;
    else if (e.type == GTOS_INPUT_POINTER_MOVE) ++inputMoves;
    else if (e.type == GTOS_INPUT_KEY_DOWN || e.type == GTOS_INPUT_KEY_UP) ++inputKeys;
    else if (e.type == GTOS_INPUT_RESET) ++inputReset;
    else return 0x52000000U | 457;
    if (e.type == GTOS_INPUT_POINTER_DOWN || e.type == GTOS_INPUT_POINTER_UP ||
        e.type == GTOS_INPUT_POINTER_MOVE) {
        // Coordinates are client-content relative, so a real press inside a
        // 16x16 window can only ever be in range.
        if (e.x < 0 || e.y < 0 || e.x >= (int)RESOURCE_WIDTH ||
            e.y >= (int)RESOURCE_HEIGHT) return 0x52000000U | 458;
    }
    return 0;
}

// Stage 1: the copy matrix, run against a queue the real desktop filled.
static int input_copy_stage(unsigned h) {
    unsigned depth = 0, lifecycle = 0, cases = 0;
    const int started = call(GTOS_SYS_TICKS);
    CHECK(started >= 0, 430);
    // The harness hovers onto the client area, presses and releases, so three
    // records are the contract. Waiting for the third and then for a quiet
    // window is what makes the multi-record case below a real multi-record
    // copy instead of a race with the release still in flight.
    int lastChange = started;
    unsigned last = 0;
    for (;;) {
        const int now = call(GTOS_SYS_TICKS);
        CHECK(now >= started && now - started < 200, 431);
        CHECK(query_info(h, depth, lifecycle) == 0, 432);
        if (depth != last) { last = depth; lastChange = now; }
        if (depth >= 3) break;
        // A quieter fallback: at least two records and nothing new for 40 ticks.
        if (depth >= 2 && now - lastChange >= 40) break;
        call(GTOS_SYS_YIELD);
    }
    const unsigned depth0 = depth;
    CHECK(depth0 >= 3, 433);
    CHECK(lifecycle == GTOS_WINDOW_LIVE, 434);
    // The multi-record destination uses the number of records that really
    // arrived, never the count cap.
    unsigned multi = depth0;
    if (multi > GTOS_INPUT_READ_LIMIT) multi = GTOS_INPUT_READ_LIMIT;

    // (a) A read-only page must refuse the copy.
    GtosInputReadRequest read = {GTOS_WINDOW_ABI_VERSION, h, 1,
                                 vmBase + 2 * GTOS_VM_PAGE_BYTES};
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 435);
    // A refused copy must not consume: the depth may only grow from a later
    // arrival, never shrink.
    CHECK(query_info(h, depth, lifecycle) == 0 && depth >= depth0, 436);
    cases += 2;

    // (b) A destination that crosses into an unmapped page must refuse it. One
    // record is 16 bytes, so starting 8 bytes before the page end crosses.
    read.destination = vmBase + GTOS_VM_PAGE_BYTES - 8;
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 437);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth >= depth0, 438);
    cases += 2;

    // (c) The destination starts 8 bytes before a read-only page, so the bytes
    // for the records that really exist cross the boundary and the whole copy
    // must be refused, leaving both pages untouched.
    unsigned char *third = vmPage(3);
    for (unsigned i = 4088; i < GTOS_VM_PAGE_BYTES; ++i) third[i] = 0x5a;
    read.count = multi;
    read.destination = vmBase + 4 * GTOS_VM_PAGE_BYTES - 8;
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 439);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth >= depth0, 440);
    // The refused copy would have covered multi * 16 bytes: 8 of them in the
    // writable page and the rest in the read-only page. Both parts are verified
    // in full, so "the whole target was untouched" is measured rather than
    // sampled from the head of the second page.
    const unsigned writablePart = GTOS_VM_PAGE_BYTES - 4088;
    const unsigned readonlyPart = multi * GTOS_INPUT_EVENT_BYTES - writablePart;
    CHECK(untouched(third + 4088, writablePart, 0x5a), 441);
    CHECK(untouched(vmPage(4), readonlyPart, 0xa5), 442);
    SAY("GTOS NATIVE WINDOW CROSS READONLY V1 ");
    sayNumber(multi * GTOS_INPUT_EVENT_BYTES);
    SAY(" ");
    sayNumber(writablePart);
    SAY(" ");
    sayNumber(readonlyPart);
    SAY("\n");
    cases += 4;

    // (d) Case (a)'s address and length with only the permission changed: the
    // copy now succeeds, which proves (a) was refused because of the page
    // permission. It is deliberately NOT a differential of (c), which uses a
    // different page and a multi-record length.
    CHECK(vm_range(2 * GTOS_VM_PAGE_BYTES, GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE) == 0, 443);
    // `depth` was refreshed by the INFO above, so this is the depth the copy
    // below is measured against.
    unsigned before = depth;
    read.count = 1;
    read.destination = vmBase + 2 * GTOS_VM_PAGE_BYTES;
    CHECK(iread(&read) == 1, 444);
    // A single record was delivered, so the depth must have dropped by at least
    // one. Events can still arrive from the desktop, so the exact-drop equality
    // is proved on the quiescent queues of stages 2 and 3 instead.
    CHECK(query_info(h, depth, lifecycle) == 0 && depth < before, 445);
    {
        const int shape = classify(*(const GtosInputEvent *)vmPage(2));
        if (shape) return shape;
    }
    cases += 3;

    // (e) The request and its output share one buffer. The record must overwrite
    // the request it was asked with, and be consumed.
    before = depth;
    GtosInputReadRequest *self = (GtosInputReadRequest *)shared;
    *self = (GtosInputReadRequest){GTOS_WINDOW_ABI_VERSION, h, 1, (unsigned)shared};
    CHECK(iread(self) == 1, 446);
    const GtosInputEvent *landed = (const GtosInputEvent *)shared;
    // The destination word is gone: no input record can carry a data address in
    // its y field, so this is the overwrite, not a coincidence.
    CHECK(landed->y != (int)(unsigned)shared, 447);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth < before, 448);
    {
        const int shape = classify(*landed);
        if (shape) return shape;
    }
    cases += 3;

    // (f) A sentinel-filled destination must only change by the records written.
    memset(sentinelled, 0xa5, sizeof(sentinelled));
    before = depth;
    read.count = 1;
    read.destination = (unsigned)sentinelled;
    CHECK(iread(&read) == 1, 449);
    for (unsigned i = GTOS_INPUT_EVENT_BYTES; i < sizeof(sentinelled); ++i)
        CHECK(sentinelled[i] == 0xa5, 450);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth < before, 451);
    {
        const int shape = classify(*(const GtosInputEvent *)sentinelled);
        if (shape) return shape;
    }
    cases += 3;

    // (g) Drain anything left over, then require that the real pointer traffic
    // arrived: the hover move, the press and the release.
    int quiet = call(GTOS_SYS_TICKS);
    CHECK(quiet >= 0, 452);
    for (;;) {
        CHECK(call(GTOS_SYS_TICKS) - quiet < 200, 453);
        memset(inbox, 0xa5, sizeof(inbox));
        read.count = GTOS_INPUT_READ_LIMIT;
        read.destination = (unsigned)inbox;
        const int n = iread(&read);
        CHECK(n >= 0 && n <= (int)GTOS_INPUT_READ_LIMIT, 454);
        if (n == 0) {
            if (call(GTOS_SYS_TICKS) - quiet >= 20) break;
            call(GTOS_SYS_YIELD);
            continue;
        }
        quiet = call(GTOS_SYS_TICKS);
        const GtosInputEvent *events = (const GtosInputEvent *)inbox;
        for (int i = 0; i < n; ++i) {
            const int shape = classify(events[i]);
            if (shape) return shape;
        }
    }
    CHECK(inputDown >= 1 && inputUp >= 1, 455);
    CHECK(inputMoves >= 1, 456);
    // The harness sent no keys and the focus has not moved yet, so these must
    // still be zero; a stray RESET here would be a routing defect.
    CHECK(inputKeys == 0 && inputReset == 0, 459);
    requiredMask |= REQ_INPUT_COPY;
    STAGE("INPUT_COPY_DONE", cases + inputSeen);
    return 0;
}

// Stage 2: a real focus change leaves exactly one RESET, and a refused read of
// it must not consume it.
static int reset_copy_stage(unsigned h) {
    unsigned depth = 0, lifecycle = 0, cases = 0;
    const int started = call(GTOS_SYS_TICKS);
    CHECK(started >= 0, 470);
    for (;;) {
        CHECK(call(GTOS_SYS_TICKS) - started < 300, 471);
        CHECK(query_info(h, depth, lifecycle) == 0, 472);
        if (depth) break;
        call(GTOS_SYS_YIELD);
    }
    // The focus change dropped every unread record, so only the RESET is left.
    const unsigned depth0 = depth;
    CHECK(depth0 == 1, 473);
    CHECK(lifecycle == GTOS_WINDOW_LIVE, 474);

    GtosInputReadRequest read = {GTOS_WINDOW_ABI_VERSION, h, 1, 0};
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 475);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth == depth0, 476);
    read.destination = (unsigned)&resource_oracles_rw_limit;
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 477);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth == depth0, 478);
    cases += 4;

    // The same pending RESET is still there, and it is read exactly once.
    memset(inbox, 0xa5, sizeof(inbox));
    read.destination = (unsigned)inbox;
    CHECK(iread(&read) == 1, 479);
    CHECK(((const GtosInputEvent *)inbox)->type == GTOS_INPUT_RESET, 480);
    CHECK(((const GtosInputEvent *)inbox)->code == 0, 481);
    CHECK(((const GtosInputEvent *)inbox)->x == 0 && ((const GtosInputEvent *)inbox)->y == 0, 482);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth == 0, 483);
    memset(inbox, 0xa5, sizeof(inbox));
    CHECK(iread(&read) == 0, 484);
    CHECK(untouched(inbox, sizeof(inbox), 0xa5), 485);
    cases += 7;

    requiredMask |= REQ_RESET_COPY;
    STAGE("RESET_COPY_DONE", cases);
    return 0;
}

// Stage 3: the desktop's request-close leaves exactly one CLOSE, and a refused
// read of it must not consume it either.
static int close_copy_stage(unsigned h) {
    unsigned depth = 0, lifecycle = 0, cases = 0;
    const int started = call(GTOS_SYS_TICKS);
    CHECK(started >= 0, 490);
    for (;;) {
        CHECK(call(GTOS_SYS_TICKS) - started < 300, 491);
        CHECK(query_info(h, depth, lifecycle) == 0, 492);
        if (lifecycle == GTOS_WINDOW_CLOSING) break;
        call(GTOS_SYS_YIELD);
    }
    CHECK(depth == 1, 493);

    GtosInputReadRequest read = {GTOS_WINDOW_ABI_VERSION, h, 1, 0};
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_BAD_ADDRESS, 494);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth == 1 &&
          lifecycle == GTOS_WINDOW_CLOSING, 495);
    cases += 2;

    memset(inbox, 0xa5, sizeof(inbox));
    read.destination = (unsigned)inbox;
    CHECK(iread(&read) == 1, 496);
    CHECK(((const GtosInputEvent *)inbox)->type == GTOS_INPUT_CLOSE, 497);
    CHECK(((const GtosInputEvent *)inbox)->code == 0, 498);
    CHECK(query_info(h, depth, lifecycle) == 0 && depth == 0 &&
          lifecycle == GTOS_WINDOW_CLOSING, 499);
    memset(inbox, 0xa5, sizeof(inbox));
    CHECK(iread(&read) == 0, 500);
    CHECK(untouched(inbox, sizeof(inbox), 0xa5), 501);
    cases += 6;

    requiredMask |= REQ_CLOSE_COPY;
    STAGE("CLOSE_COPY_DONE", cases);
    return 0;
}

static int window_requests() {
    const unsigned limit = (unsigned)&resource_oracles_rw_limit;
    const int rejected = rejected_creates();
    if (rejected) return rejected;

    GtosWindowCreateRequest create = {GTOS_WINDOW_ABI_VERSION, RESOURCE_WIDTH, RESOURCE_HEIGHT,
                                      RESOURCE_WIDTH * 4,
                                      GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED};
    const int handle = wcreate(&create);
    CHECK(handle > 0, 320);
    const unsigned h = (unsigned)handle;
    GtosWindowControlRequest control = {GTOS_WINDOW_ABI_VERSION, h};

    // --- SUBMIT rejections ---------------------------------------------------
    GtosWindowSubmitRequest submit = {GTOS_WINDOW_ABI_VERSION, h, 0, 0, 0};
    CHECK(rawsubmit(&submit, 19) == GTOS_WINDOW_ERR_BAD_SIZE, 340);
    CHECK(rawsubmit(&submit, 21) == GTOS_WINDOW_ERR_BAD_SIZE, 341);
    CHECK(call(GTOS_SYS_WINDOW_SUBMIT, 0, sizeof(submit)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 342);
    submit.version = 0;
    CHECK(rawsubmit(&submit) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 343);
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = h + 97;
    submit.length = GTOS_WINDOW_WRITE_LIMIT;
    submit.source = (unsigned)chunk;
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_HANDLE, 344);
    submit.handle = h;
    submit.length = GTOS_WINDOW_WRITE_LIMIT + 1;
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_SIZE, 345);
    submit.length = 0;
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_SIZE, 346);
    // An offset that is not the exact next sequential position is a state
    // error: the ABI rejects restart instead of silently overwriting.
    submit.length = GTOS_WINDOW_WRITE_LIMIT;
    submit.offset = RESOURCE_WIDTH * RESOURCE_HEIGHT * 4;
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_STATE, 347);
    submit.offset = 0;
    submit.source = 0;
    CHECK(rawsubmit(&submit) == GTOS_WINDOW_ERR_BAD_ADDRESS, 348);
    submit.source = 0x100000U;
    CHECK(rawsubmit(&submit) == GTOS_WINDOW_ERR_BAD_ADDRESS, 349);
    submit.source = limit - 8;
    CHECK(rawsubmit(&submit) == GTOS_WINDOW_ERR_BAD_ADDRESS, 350);
    // A non-sequential offset is a state error, not a range error.
    submit.source = (unsigned)chunk;
    submit.offset = GTOS_WINDOW_WRITE_LIMIT;
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_STATE, 351);
    // Non-premultiplied pixels are refused before any draft byte changes.
    submit.offset = 0;
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0xff; chunk[i + 1] = 0x00; chunk[i + 2] = 0x00; chunk[i + 3] = 0x00;
    }
    CHECK(wsubmit(&submit) == GTOS_WINDOW_ERR_BAD_PIXELS, 357);
    // The draft is untouched by the refusal, so offset 0 is still the exact next
    // sequential position: a consumed draft would make this a state error.
    fill(255);
    submit.source = (unsigned)frame;
    CHECK(wsubmit(&submit) == (int)GTOS_WINDOW_WRITE_LIMIT, 358);
    // PRESENT before a complete draft is a state error.
    CHECK(wpresent(&control) == GTOS_WINDOW_ERR_BAD_STATE, 352);
    CHECK(rawpresent(&control, 7) == GTOS_WINDOW_ERR_BAD_SIZE, 353);
    CHECK(call(GTOS_SYS_WINDOW_PRESENT, 0, sizeof(control)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 354);
    control.version = 0;
    CHECK(rawpresent(&control) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 355);
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = h + 97;
    CHECK(wpresent(&control) == GTOS_WINDOW_ERR_BAD_HANDLE, 356);
    control.handle = h;

    // --- three frames, three different generations ---------------------------
    // The first chunk is already in the draft from the refusal check above.
    static unsigned char first[RESOURCE_WIDTH * RESOURCE_HEIGHT * 4];
    const int f1 = publish(h, 255, GTOS_WINDOW_WRITE_LIMIT);
    if (f1) return f1;
    for (unsigned i = 0; i < sizeof(first); ++i) first[i] = frame[i];
    GtosWindowInfoRequest info = {GTOS_WINDOW_ABI_VERSION, h, (unsigned)boundary};
    CHECK(winfo(&info) == 0, 360);
    const GtosWindowInfoResponse afterFirst = *(GtosWindowInfoResponse *)boundary;
    CHECK(afterFirst.handle == h, 361);
    CHECK(afterFirst.lifecycle == GTOS_WINDOW_LIVE, 362);
    CHECK(afterFirst.show == GTOS_WINDOW_SHOW_PUBLISHED, 363);
    CHECK(afterFirst.width == RESOURCE_WIDTH && afterFirst.height == RESOURCE_HEIGHT, 364);
    CHECK(afterFirst.frame_generation == 1, 365);
    CHECK(afterFirst.queue_depth == 0 && afterFirst.dropped == 0, 366);
    // Repeating PRESENT without a new draft is refused.
    CHECK(wpresent(&control) == GTOS_WINDOW_ERR_BAD_STATE, 367);
    const int f2 = publish(h, 128, 0);
    if (f2) return f2;
    const int f3 = publish(h, 64, 0);
    if (f3) return f3;
    CHECK(winfo(&info) == 0, 368);
    const GtosWindowInfoResponse afterThree = *(GtosWindowInfoResponse *)boundary;
    CHECK(afterThree.frame_generation == 3, 369);
    CHECK(afterThree.show == GTOS_WINDOW_SHOW_PUBLISHED, 370);
    // The three frames really carried different pixels.
    CHECK(!equal(first, frame, sizeof(first)), 371);
    CHECK(crc32(first, sizeof(first)) != crc32(frame, sizeof(frame)), 372);

    // --- INFO and INPUT_READ rejections --------------------------------------
    CHECK(rawinfo(&info, 11) == GTOS_WINDOW_ERR_BAD_SIZE, 380);
    CHECK(rawinfo(&info, 13) == GTOS_WINDOW_ERR_BAD_SIZE, 381);
    CHECK(call(GTOS_SYS_WINDOW_INFO, 0, sizeof(info)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 382);
    info.version = 0;
    CHECK(rawinfo(&info) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 383);
    info.version = GTOS_WINDOW_ABI_VERSION;
    info.handle = h + 97;
    CHECK(winfo(&info) == GTOS_WINDOW_ERR_BAD_HANDLE, 384);
    info.handle = h;
    info.result_va = 0;
    CHECK(rawinfo(&info) == GTOS_WINDOW_ERR_BAD_ADDRESS, 385);
    info.result_va = 0x100000U;
    CHECK(rawinfo(&info) == GTOS_WINDOW_ERR_BAD_ADDRESS, 386);
    info.result_va = limit;
    CHECK(rawinfo(&info) == GTOS_WINDOW_ERR_BAD_ADDRESS, 387);
    // Restore a valid output address. Leaving this at `limit` made every later
    // INFO fail, which silently skipped the copy cases that depend on it.
    info.result_va = (unsigned)boundary;
    CHECK(winfo(&info) == 0, 388);

    GtosInputReadRequest read = {GTOS_WINDOW_ABI_VERSION, h, GTOS_INPUT_READ_LIMIT,
                                 (unsigned)inbox};
    memset(inbox, 0xa5, sizeof(inbox));
    CHECK(iread(&read) == 0, 390);
    CHECK(untouched(inbox, sizeof(inbox), 0xa5), 391);
    CHECK(rawiread(&read, 15) == GTOS_WINDOW_ERR_BAD_SIZE, 392);
    CHECK(rawiread(&read, 17) == GTOS_WINDOW_ERR_BAD_SIZE, 393);
    CHECK(call(GTOS_SYS_INPUT_READ, 0, sizeof(read)) == GTOS_WINDOW_ERR_BAD_ADDRESS, 394);
    read.version = 0;
    CHECK(rawiread(&read) == GTOS_WINDOW_ERR_UNSUPPORTED_VERSION, 395);
    read.version = GTOS_WINDOW_ABI_VERSION;
    read.handle = h + 97;
    CHECK(iread(&read) == GTOS_WINDOW_ERR_BAD_HANDLE, 396);
    read.handle = h;
    read.count = 0;
    CHECK(iread(&read) == GTOS_WINDOW_ERR_BAD_SIZE, 397);
    read.count = GTOS_INPUT_READ_LIMIT + 1;
    CHECK(iread(&read) == GTOS_WINDOW_ERR_BAD_SIZE, 398);
    read.count = GTOS_INPUT_READ_LIMIT;
    // An empty queue copies nothing, so a bad destination is not even reached;
    // the destination checks happen below on a queue that really holds records.
    read.destination = 0;
    CHECK(iread(&read) == 0, 399);
    read.destination = (unsigned)inbox;

    // --- real input from the real desktop ------------------------------------
    // The desktop only routes to a focused window, so the harness moves the
    // pointer onto this window and presses. Nothing here is a fixture: every
    // record below arrives through the kernel copy from the actual PS/2 queue,
    // and every destination is a page this process really owns.
    SAY("GTOS NATIVE WINDOW READY V1\n");
    // Report the tick the first wait starts at, so the harness can prove the
    // kernel's demo budget was real and not assumed.
    {
        const int started = call(GTOS_SYS_TICKS);
        CHECK(started >= 0, 409);
        char stamp[48];
        unsigned stampBytes = 0;
        static const char head[] = "GTOS NATIVE WINDOW WAIT V1 ";
        for (unsigned i = 0; i < sizeof(head) - 1; ++i) stamp[stampBytes++] = head[i];
        unsigned value = (unsigned)started;
        char digits[10];
        unsigned count = 0;
        do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
        while (count) stamp[stampBytes++] = digits[--count];
        stamp[stampBytes++] = '\n';
        say(stamp, stampBytes);
    }
    const int vmReady = vm_setup();
    if (vmReady) return vmReady;
    const int copied = input_copy_stage(h);
    if (copied) return copied;
    const int reset = reset_copy_stage(h);
    if (reset) return reset;
    const int closed = close_copy_stage(h);
    if (closed) return closed;

    // The application now closes the object the desktop asked it to close.
    CHECK(wclose(&control) == 0, 420);
    CHECK(winfo(&info) == GTOS_WINDOW_ERR_BAD_HANDLE, 421);
    CHECK(iread(&read) == GTOS_WINDOW_ERR_BAD_HANDLE, 422);
    CHECK(wpresent(&control) == GTOS_WINDOW_ERR_BAD_HANDLE, 423);
    CHECK(wclose(&control) == GTOS_WINDOW_ERR_BAD_HANDLE, 424);
    // No required stage may have been skipped: the PASS line is gated on this.
    CHECK(requiredMask == REQ_ALL, 425);
    return 0;
}

extern "C" int png_guest_main(void) {
    static const char begin[] = "GTOS NATIVE WINDOW START V1\n";    static const char pass[] = "GTOS NATIVE WINDOW PASS CREATE SUBMIT PRESENT INPUT CLOSE V1\n";
    CHECK(call(GTOS_SYS_ABI) == GTOS_NATIVE_ABI_VERSION, 1);
    CHECK(call(0xffffffffU) == GTOS_ERR_UNSUPPORTED, 2);
    CHECK(call(GTOS_SYS_WRITE, (unsigned)begin, sizeof(begin) - 1) == (int)sizeof(begin) - 1, 3);
    CHECK(untouched(context, sizeof(context), 0) && initialized == 0x47544f53U, 4);

    // The qualified PNG path is unchanged: read the public resource, decode it.
    GtosResourceInfo resource;
    CHECK(call(GTOS_SYS_RESOURCE_INFO, GTOS_RESOURCE_PUBLIC_PNG_ID, (unsigned)&resource) == 0, 5);
    CHECK(resource.version == GTOS_RESOURCE_ABI_VERSION && resource.type == GTOS_RESOURCE_TYPE_PNG &&
              resource.bytes == RESOURCE_PNG_BYTES && resource.bytes <= sizeof(input), 6);
    unsigned offset = 0;
    while (offset < resource.bytes) {
        GtosResourceReadRequest q = {GTOS_RESOURCE_ABI_VERSION, GTOS_RESOURCE_PUBLIC_PNG_ID, offset,
                                     (unsigned)(input + offset), GTOS_RESOURCE_READ_LIMIT};
        const int n = call(GTOS_SYS_RESOURCE_READ, (unsigned)&q, sizeof(q));
        const unsigned expected = resource.bytes - offset < GTOS_RESOURCE_READ_LIMIT
                                      ? resource.bytes - offset
                                      : GTOS_RESOURCE_READ_LIMIT;
        CHECK(n == (int)expected, 7);
        offset += (unsigned)n;
        CHECK(call(GTOS_SYS_YIELD) == 0, 8);
    }
    CHECK(crc32(input, offset) == RESOURCE_PNG_CRC32, 9);
    gtos_png_requirements needed;
    memset(&needed, 0x5a, sizeof(needed));
    CHECK(gtos_png_context_bytes() <= sizeof(context) &&
              gtos_png_inspect(context, sizeof(context), input, resource.bytes, &needed) ==
                  GTOS_PNG_OK, 10);
    CHECK(needed.width == RESOURCE_WIDTH && needed.height == RESOURCE_HEIGHT, 11);
    memset(bgra, 0xa5, sizeof(bgra));
    memset(rgba, 0xa5, sizeof(rgba));
    CHECK(gtos_png_decode_rgba(context, sizeof(context), input, resource.bytes, work,
                               sizeof(work), bgra, sizeof(bgra), RESOURCE_WIDTH * 4, rgba,
                               sizeof(rgba), RESOURCE_WIDTH * 4, &needed) == GTOS_PNG_OK, 12);
    CHECK(equal(bgra, resource_expected_bgra, sizeof(bgra)) &&
              equal(rgba, resource_expected_premul_rgba, sizeof(rgba)), 13);

    const int windows = window_requests();
    if (windows) return windows;
    // Both paths really ran: the normal calls went through the exported SDK
    // wrapper and the malformed-request negatives went raw. A zero on either
    // side would mean this build proved only half of the claim.
    CHECK(sdkCalls > 0 && rawCalls > 0, 15);
    SAY("GTOS NATIVE WINDOW SDK CALLS V1 ");
    sayNumber(sdkCalls);
    SAY(" ");
    sayNumber(rawCalls);
    SAY("\n");
    CHECK(call(GTOS_SYS_WRITE, (unsigned)pass, sizeof(pass) - 1) == (int)sizeof(pass) - 1, 14);
    return 0;
}
