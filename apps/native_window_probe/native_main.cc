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
#include <process/native_window_adapter.h>
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
#if !defined(__i386__) || !defined(__GTOS__) || defined(__linux__) || defined(_WIN32)
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
#define REQ_ADAPTER 8U
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
// The line is composed in one buffer and written once: composing it from three
// writes let the desktop's own console output land between the "V1 " and the
// count, which split one stage line into two and made the harness read the stage
// as never reported. The buffer holds the longest prefix, the longest stage name
// and ten digits with room to spare.
#define STAGE_PREFIX "GTOS NATIVE WINDOW STAGE "
#define STAGE_VERSION " V1 "
static void stageLine(const char *name, unsigned nameLength, unsigned cases) {
    char line[64];
    unsigned used = 0;
    for (unsigned i = 0; i < sizeof(STAGE_PREFIX) - 1; ++i) line[used++] = STAGE_PREFIX[i];
    for (unsigned i = 0; i < nameLength; ++i) line[used++] = name[i];
    for (unsigned i = 0; i < sizeof(STAGE_VERSION) - 1; ++i) line[used++] = STAGE_VERSION[i];
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + cases % 10U);
        cases /= 10U;
    } while (cases);
    for (unsigned i = 0; i < count; ++i) line[used++] = digits[count - 1 - i];
    line[used++] = '\n';
    say(line, used);
}
#define STAGE(name, cases) stageLine(name, (unsigned)(sizeof(name) - 1), cases)
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
//
// `rawclose` is the one well-formed raw call: the adapter lifecycle stage needs
// a CLOSE that does not travel through the adapter, because it is standing in
// for somebody else destroying the window behind the object's back.
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
// A CLOSE that must not pass through the adapter: it plays the part of another
// holder of the same owner destroying the window behind the object's back.
static int rawclose(const GtosWindowControlRequest *q, unsigned bytes = sizeof(*q)) {
    ++rawCalls;
    return call(GTOS_SYS_WINDOW_CLOSE, (unsigned)q, bytes);
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

// ---- the user-space adapter, driven end to end -----------------------------
// Everything here goes through gtos::process::NativeWindowAdapter, so the six
// calls are reached through the object a UI layer would actually hold. The
// wire-level negatives above stay on the raw path; this stage is about object
// identity, ownership and lifecycle.
static unsigned char adapterFrame[16 * 16 * 4];
static GtosWindowInfoResponse adapterInfo;
static GtosInputEvent adapterEvents[4];

// Bounds for the two waits the harness drives. Both are ticks of the kernel's own
// counter, so a run that never receives the request-close fails instead of
// hanging, and the hold cannot be confused with an unbounded sleep. The wait is
// generous because the harness may need a couple of presses before the desktop
// has bound the new object to a window; a repeat press on an already-closing
// window is a no-op in the desktop, so extra presses cannot corrupt the state.
#define ADAPTER_CLOSE_WAIT 3000
#define ADAPTER_CLOSE_HOLD 300

static void adapter_fill(void) {
    for (unsigned i = 0; i < sizeof(adapterFrame); i += 4) {
        adapterFrame[i + 0] = 0x20;
        adapterFrame[i + 1] = 0x40;
        adapterFrame[i + 2] = 0x60;
        adapterFrame[i + 3] = 0xff;
    }
}

// One object whose window somebody else destroys. The adapter cannot know that
// happened, so it must still claim the handle until a call comes back BAD_HANDLE;
// that answer - and only that answer - drops the local claim.
static int adapter_stage(void) {
    gtos::process::NativeWindowAdapter window;
    unsigned cases = 0;
    // A default adapter holds nothing: no window was created by construction.
    CHECK(!window.valid() && window.handle() == 0, 600);
    CHECK(window.destroyed(), 601);
    // The root content view shares this object's identity. There is no tree, so
    // there is exactly one view and it is this window.
    CHECK(window.root_view() == &window, 602);

    // With no handle the kernel - not the adapter - refuses the call.
    CHECK(window.Present() == GTOS_WINDOW_ERR_BAD_HANDLE, 603);
    CHECK(window.ReadInput(adapterEvents, 4) == GTOS_WINDOW_ERR_BAD_HANDLE, 604);

    CHECK(window.Create(16, 16) > 0, 605);
    CHECK(window.valid() && window.width() == 16 && window.height() == 16, 606);
    const unsigned stale = window.kernel_handle();
    CHECK(stale != 0, 607);
    cases += 8;

    adapter_fill();

    // --- BAD_STATE is not a lifecycle signal ---------------------------------
    // The kernel returns it for an incomplete draft just as it does for a window
    // that is no longer LIVE, so none of these may set closing.
    CHECK(window.Present() == GTOS_WINDOW_ERR_BAD_STATE, 608);
    CHECK(window.valid() && !window.closing(), 609);
    CHECK(window.Submit(adapterFrame, GTOS_WINDOW_WRITE_LIMIT, 0) ==
              (int)GTOS_WINDOW_WRITE_LIMIT, 610);
    CHECK(window.Present() == GTOS_WINDOW_ERR_BAD_STATE, 611);
    CHECK(window.valid() && !window.closing(), 612);
    // The refused publish did not discard the chunk that was already accepted.
    memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
    CHECK(window.Info(&adapterInfo) == 0 &&
              adapterInfo.progress == GTOS_WINDOW_WRITE_LIMIT, 613);
    CHECK(adapterInfo.lifecycle == GTOS_WINDOW_LIVE &&
              adapterInfo.show == GTOS_WINDOW_SHOW_DRAFTING, 614);
    // An offset that is not the exact next position is refused, not restarted.
    CHECK(window.Submit(adapterFrame, GTOS_WINDOW_WRITE_LIMIT,
                        sizeof(adapterFrame)) == GTOS_WINDOW_ERR_BAD_STATE, 615);
    CHECK(window.valid() && !window.closing(), 616);
    cases += 7;

    // The rest of the frame, still in the order the ABI requires.
    for (unsigned offset = GTOS_WINDOW_WRITE_LIMIT; offset < sizeof(adapterFrame);
         offset += GTOS_WINDOW_WRITE_LIMIT) {
        CHECK(window.Submit(adapterFrame + offset, GTOS_WINDOW_WRITE_LIMIT,
                            offset) == (int)GTOS_WINDOW_WRITE_LIMIT, 617);
        ++cases;
    }
    CHECK(window.Present() == 0, 618);
    // Publishing again with no fresh draft is the same BAD_STATE on a LIVE window.
    CHECK(window.Present() == GTOS_WINDOW_ERR_BAD_STATE, 619);
    CHECK(window.valid() && !window.closing(), 620);
    cases += 3;

    // Every refusal above left the published frame and the window intact.
    memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
    CHECK(window.Info(&adapterInfo) == 0, 621);
    CHECK(adapterInfo.handle == stale, 622);
    CHECK(adapterInfo.width == 16 && adapterInfo.height == 16, 623);
    CHECK(adapterInfo.lifecycle == GTOS_WINDOW_LIVE, 624);
    CHECK(adapterInfo.show == GTOS_WINDOW_SHOW_PUBLISHED &&
              adapterInfo.frame_generation == 1 && adapterInfo.progress == 0, 625);
    CHECK(!window.closing(), 626);
    cases += 6;

    // --- a failed INFO must not adopt the caller's stale bytes --------------
    // The buffer is poisoned with what a wrong adoption would show, so the two
    // checks below can tell "did not read out" from "read out and happened to
    // agree". 0x100000 is an address this process does not own; the wire tests
    // above already prove the kernel refuses it.
    memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
    adapterInfo.lifecycle = GTOS_WINDOW_CLOSING;
    CHECK(window.Info((GtosWindowInfoResponse *)0x100000U) ==
              GTOS_WINDOW_ERR_BAD_ADDRESS, 627);
    CHECK(!window.closing() && window.valid() && window.handle() == (int)stale, 628);
    memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
    CHECK(window.Info(&adapterInfo) == 0 &&
              adapterInfo.lifecycle == GTOS_WINDOW_LIVE, 629);
    CHECK(!window.closing(), 630);
    cases += 4;

    // --- INPUT_READ into caller-owned storage --------------------------------
    memset(adapterEvents, 0xa5, sizeof(adapterEvents));
    const int events = window.ReadInput(adapterEvents, 4);
    CHECK(events >= 0 && events <= 4, 631);
    for (int index = 0; index < events; ++index) {
        CHECK(adapterEvents[index].type <= GTOS_INPUT_RESET, 632);
        ++cases;
    }
    ++cases;

    // --- the real desktop asks this very window to close ---------------------
    // The harness presses the same title-bar button it already uses for the wire
    // stage; the window it hits is this adapter's, in the same slot.
    SAY("GTOS NATIVE WINDOW ADAPTER CLOSE WAIT V1\n");
    {
        const int started = call(GTOS_SYS_TICKS);
        CHECK(started >= 0, 633);
        for (;;) {
            CHECK(call(GTOS_SYS_TICKS) - started < ADAPTER_CLOSE_WAIT, 634);
            memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
            CHECK(window.Info(&adapterInfo) == 0, 635);
            if (adapterInfo.lifecycle == GTOS_WINDOW_CLOSING) break;
            // Anything other than LIVE here would mean the window died instead of
            // being asked to close, which is a different event.
            CHECK(adapterInfo.lifecycle == GTOS_WINDOW_LIVE, 636);
            call(GTOS_SYS_YIELD);
        }
        // CLOSING is a state, not a destruction: the handle is still ours and the
        // object says so only because a successful INFO said so.
        CHECK(window.valid() && window.closing() && window.handle() == (int)stale, 637);
        CHECK(adapterInfo.handle == stale, 638);
        // The last frame is still the published one: the request did not throw the
        // front buffer away, and it did not leave a half-written draft behind.
        CHECK(adapterInfo.show == GTOS_WINDOW_SHOW_PUBLISHED &&
                  adapterInfo.frame_generation == 1 && adapterInfo.progress == 0, 639);
        cases += 7;

        // Writing and publishing are refused, and the refusals change nothing.
        CHECK(window.Submit(adapterFrame, GTOS_WINDOW_WRITE_LIMIT, 0) ==
                  GTOS_WINDOW_ERR_BAD_STATE, 640);
        CHECK(window.Present() == GTOS_WINDOW_ERR_BAD_STATE, 641);
        CHECK(window.valid() && window.closing(), 642);
        memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
        CHECK(window.Info(&adapterInfo) == 0 &&
                  adapterInfo.lifecycle == GTOS_WINDOW_CLOSING, 643);
        cases += 4;

        // The single CLOSE record the desktop queued is delivered exactly once.
        memset(adapterEvents, 0xa5, sizeof(adapterEvents));
        CHECK(window.ReadInput(adapterEvents, 4) == 1, 644);
        CHECK(adapterEvents[0].type == GTOS_INPUT_CLOSE, 645);
        CHECK(window.valid() && window.closing(), 646);
        cases += 3;
    }

    // Hold the closing window while the harness screenshots the last frame. The
    // loop keeps polling, so this is still an observation and not a blind sleep.
    SAY("GTOS NATIVE WINDOW ADAPTER CLOSING V1\n");
    {
        const int held = call(GTOS_SYS_TICKS);
        CHECK(held >= 0, 647);
        for (;;) {
            memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
            CHECK(window.Info(&adapterInfo) == 0 &&
                      adapterInfo.lifecycle == GTOS_WINDOW_CLOSING, 648);
            if (call(GTOS_SYS_TICKS) - held >= ADAPTER_CLOSE_HOLD) break;
            CHECK(call(GTOS_SYS_TICKS) - held < ADAPTER_CLOSE_HOLD + 120, 649);
            call(GTOS_SYS_YIELD);
        }
        cases += 2;
    }

    // Only the application's own CLOSE destroys it.
    CHECK(window.Close() == 0, 650);
    CHECK(!window.valid() && window.handle() == 0, 651);
    CHECK(window.destroyed() && !window.closing(), 652);
    // The old identifier is refused by the kernel, not quietly accepted.
    GtosWindowControlRequest staleRequest = {GTOS_WINDOW_ABI_VERSION, stale};
    CHECK(wpresent(&staleRequest) == GTOS_WINDOW_ERR_BAD_HANDLE, 653);
    // A second CLOSE through the adapter is BAD_HANDLE and stays cleared.
    CHECK(window.Close() == GTOS_WINDOW_ERR_BAD_HANDLE, 654);
    CHECK(!window.valid(), 655);
    cases += 6;

    // --- somebody else destroys the window behind the object's back ----------
    // Five separate objects, each closed before the next is created, because the
    // bank allows one window per owner. Each is revoked through a different call,
    // so no single method can carry the claim for the others.
    {
        gtos::process::NativeWindowAdapter lost;
        CHECK(lost.Create(16, 16) > 0, 656);
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION,
                                             lost.kernel_handle()};
        CHECK(rawclose(&external) == 0, 657);
        // The object cannot know yet, and must not pretend it does.
        CHECK(lost.valid(), 658);
        CHECK(lost.Submit(adapterFrame, GTOS_WINDOW_WRITE_LIMIT, 0) ==
                  GTOS_WINDOW_ERR_BAD_HANDLE, 659);
        CHECK(!lost.valid() && lost.handle() == 0 && lost.destroyed(), 660);
        cases += 5;
    }
    {
        gtos::process::NativeWindowAdapter lost;
        CHECK(lost.Create(16, 16) > 0, 661);
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION,
                                             lost.kernel_handle()};
        CHECK(rawclose(&external) == 0, 662);
        CHECK(lost.Present() == GTOS_WINDOW_ERR_BAD_HANDLE, 663);
        CHECK(!lost.valid() && lost.handle() == 0, 664);
        cases += 4;
    }
    {
        gtos::process::NativeWindowAdapter lost;
        CHECK(lost.Create(16, 16) > 0, 665);
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION,
                                             lost.kernel_handle()};
        CHECK(rawclose(&external) == 0, 666);
        memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
        adapterInfo.lifecycle = GTOS_WINDOW_CLOSING;
        CHECK(lost.Info(&adapterInfo) == GTOS_WINDOW_ERR_BAD_HANDLE, 667);
        CHECK(!lost.valid() && lost.handle() == 0 && !lost.closing(), 668);
        cases += 4;
    }
    {
        gtos::process::NativeWindowAdapter lost;
        CHECK(lost.Create(16, 16) > 0, 669);
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION,
                                             lost.kernel_handle()};
        CHECK(rawclose(&external) == 0, 670);
        CHECK(lost.ReadInput(adapterEvents, 4) == GTOS_WINDOW_ERR_BAD_HANDLE, 671);
        CHECK(!lost.valid() && lost.handle() == 0, 672);
        cases += 4;
    }
    {
        gtos::process::NativeWindowAdapter lost;
        CHECK(lost.Create(16, 16) > 0, 673);
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION,
                                             lost.kernel_handle()};
        CHECK(rawclose(&external) == 0, 674);
        CHECK(lost.Close() == GTOS_WINDOW_ERR_BAD_HANDLE, 675);
        CHECK(!lost.valid() && lost.handle() == 0 && lost.destroyed(), 676);
        cases += 4;
    }

    // --- a dead object can hold a new window; the old handle stays dead ------
    {
        gtos::process::NativeWindowAdapter again;
        CHECK(again.Create(16, 16) > 0, 677);
        const unsigned first = again.kernel_handle();
        GtosWindowControlRequest external = {GTOS_WINDOW_ABI_VERSION, first};
        CHECK(rawclose(&external) == 0, 678);
        CHECK(again.Present() == GTOS_WINDOW_ERR_BAD_HANDLE, 679);
        CHECK(!again.valid(), 680);
        const int fresh = again.Create(16, 16);
        CHECK(fresh > 0, 681);
        CHECK(again.valid() && again.kernel_handle() == (unsigned)fresh, 682);
        CHECK(again.kernel_handle() != first, 683);
        // The handle that was destroyed does not come back with the new window.
        GtosWindowControlRequest old = {GTOS_WINDOW_ABI_VERSION, first};
        CHECK(rawpresent(&old) == GTOS_WINDOW_ERR_BAD_HANDLE, 684);
        // The lifecycle really is LIVE again, not merely "valid".
        memset(&adapterInfo, 0xa5, sizeof(adapterInfo));
        CHECK(again.Info(&adapterInfo) == 0 &&
                  adapterInfo.lifecycle == GTOS_WINDOW_LIVE, 685);
        CHECK(!again.closing() && again.handle() == fresh, 686);
        // A refused duplicate CREATE keeps the window it already has.
        CHECK(again.Create(32, 32) == GTOS_WINDOW_ERR_LIMIT, 687);
        CHECK(again.valid() && again.kernel_handle() == (unsigned)fresh, 688);
        CHECK(again.width() == 16 && again.height() == 16, 689);
        CHECK(again.Close() == 0, 690);
        CHECK(!again.valid(), 691);
        cases += 15;
    }

    requiredMask |= REQ_ADAPTER;
    STAGE("ADAPTER_LIFECYCLE_DONE", cases);
    SAY("GTOS NATIVE WINDOW ADAPTER PASS V1\n");
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
    // The adapter stage drives the same six calls through the user-space object,
    // so it must run before the call counters are reported.
    const int adapter = adapter_stage();
    if (adapter) return adapter;
    // The adapter stage declares its own bit, so a run that reached PASS without
    // executing it is impossible even though it runs after the mask check above.
    CHECK((requiredMask & REQ_ADAPTER) != 0, 692);
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
