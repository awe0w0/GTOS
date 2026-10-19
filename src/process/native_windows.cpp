#include <process/native_windows.h>
#ifndef GTOS_WINDOW_HOST_TEST
#include <memory/criticalsection.h>
#endif

namespace gtos {
namespace process {

namespace {
#ifndef GTOS_WINDOW_HOST_TEST
typedef memory::InterruptGuard WindowGuard;
#else
// Host invariants cover pure state; real checked-copy/concurrency/display
// acceptance is exclusively the actual QEMU consumer qualification.
struct WindowGuard { WindowGuard() {} };
#endif
void ZeroBytes(unsigned char* bytes, unsigned length) {
    for (unsigned i = 0; i < length; ++i) bytes[i] = 0;
}
void CopyBytes(unsigned char* destination, const unsigned char* source, unsigned length) {
    for (unsigned i = 0; i < length; ++i) destination[i] = source[i];
}
// One static bank. A namespace-scope instance avoids the thread-safe static
// guard (and its __cxa_guard/atexit imports) in the freestanding ELF32 build.
NativeWindows bankInstance;
}

NativeWindows::NativeWindows() : nextHandle(1), available(false) {
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i) ClearSlot(slots[i]);
}

NativeWindows& NativeWindows::Instance() { return bankInstance; }

void NativeWindows::ClearSlot(Slot& slot) {
    slot.handle = slot.owner = slot.lifecycle = 0;
    slot.width = slot.height = slot.stride = slot.total = 0;
    slot.progress = slot.frameGeneration = 0;
    ZeroBytes(slot.draft, GTOS_WINDOW_MAX_BYTES);
    ZeroBytes(slot.front, GTOS_WINDOW_MAX_BYTES);
    ClearQueue(slot);
    slot.lifecycle = GTOS_WINDOW_DEAD;
}

void NativeWindows::ClearQueue(Slot& slot) {
    slot.queueHead = slot.queueCount = slot.dropped = 0;
    slot.resetPending = slot.closePending = false;
}

void NativeWindows::SetAvailable(bool value) {
    WindowGuard guard;
    available = value;
}

NativeWindows::Slot* NativeWindows::Find(unsigned ownerId, unsigned handle) {
    if (!ownerId || !handle) return 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i) {
        Slot& slot = slots[i];
        if (slot.handle == handle && slot.owner == ownerId &&
            slot.lifecycle != GTOS_WINDOW_DEAD) return &slot;
    }
    return 0;
}

const NativeWindows::Slot* NativeWindows::Find(unsigned ownerId, unsigned handle) const {
    return const_cast<NativeWindows*>(this)->Find(ownerId, handle);
}

NativeWindows::Slot* NativeWindows::FreeSlot() {
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].lifecycle == GTOS_WINDOW_DEAD) return &slots[i];
    return 0;
}

void NativeWindows::ScrubDraft(Slot& slot) {
    ZeroBytes(slot.draft, GTOS_WINDOW_MAX_BYTES);
    slot.progress = 0;
}

void NativeWindows::DropNormal(Slot& slot) {
    unsigned dropped = slot.queueCount;
    slot.queueHead = slot.queueCount = 0;
    unsigned before = slot.dropped;
    slot.dropped += dropped;
    if (slot.dropped < before) slot.dropped = 0xffffffffU;
}

// The return value is about THIS call only: it reports whether the normal ring
// had to be reset. Whether a reset was already pending is a separate question.
bool NativeWindows::PushNormal(Slot& slot, const GtosInputEvent& event) {
    if (slot.lifecycle != GTOS_WINDOW_LIVE) return false;
    bool overflowed = false;
    if (slot.queueCount >= GTOS_INPUT_NORMAL_ENTRIES) {
        DropNormal(slot);
        slot.resetPending = true;
        overflowed = true;
    }
    unsigned index = (slot.queueHead + slot.queueCount) % GTOS_INPUT_NORMAL_ENTRIES;
    slot.queue[index] = event;
    ++slot.queueCount;
    return overflowed;
}

int NativeWindows::Create(unsigned ownerId, const GtosWindowCreateRequest& request) {
    WindowGuard guard;
    if (!available) return GTOS_WINDOW_ERR_BAD_STATE;
    if (request.version != GTOS_WINDOW_ABI_VERSION) return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
    if (!ownerId) return GTOS_WINDOW_ERR_BAD_STATE;
    if (request.format != GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED)
        return GTOS_WINDOW_ERR_UNSUPPORTED_FORMAT;
    if (!request.width || request.width > GTOS_WINDOW_MAX_WIDTH) return GTOS_WINDOW_ERR_RANGE;
    if (!request.height || request.height > GTOS_WINDOW_MAX_HEIGHT) return GTOS_WINDOW_ERR_RANGE;
    // Bounded before multiplication: both factors are already <= 32.
    unsigned total = request.width * request.height * 4U;
    if (total > GTOS_WINDOW_MAX_BYTES) return GTOS_WINDOW_ERR_TOO_LARGE;
    if (request.stride != request.width * 4U) return GTOS_WINDOW_ERR_RANGE;
    unsigned owned = 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].owner == ownerId && slots[i].lifecycle != GTOS_WINDOW_DEAD) ++owned;
    if (owned >= GTOS_WINDOW_MAX_PER_OWNER) return GTOS_WINDOW_ERR_LIMIT;
    Slot* slot = FreeSlot();
    if (!slot) return GTOS_WINDOW_ERR_LIMIT;
    // Handle exhaustion is its own condition, distinct from capacity limits.
    if (nextHandle == 0 || nextHandle > 0x7fffffffU) return GTOS_WINDOW_ERR_HANDLE_EXHAUSTED;
    ClearSlot(*slot);
    slot->handle = nextHandle++;
    slot->owner = ownerId;
    slot->lifecycle = GTOS_WINDOW_LIVE;
    slot->width = request.width;
    slot->height = request.height;
    slot->stride = request.stride;
    slot->total = total;
    slot->progress = 0;
    slot->frameGeneration = 0;
    return (int)slot->handle;
}

int NativeWindows::ValidateSubmit(const Slot& slot,
                                 const GtosWindowSubmitRequest& request) const {
    if (request.version != GTOS_WINDOW_ABI_VERSION) return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
    if (slot.lifecycle != GTOS_WINDOW_LIVE) return GTOS_WINDOW_ERR_BAD_STATE;
    // First version rejects restart: only the exact next sequential offset.
    if (request.offset != slot.progress) return GTOS_WINDOW_ERR_BAD_STATE;
    if (!request.length || request.length > GTOS_WINDOW_WRITE_LIMIT)
        return GTOS_WINDOW_ERR_BAD_SIZE;
    if (request.length % 4U) return GTOS_WINDOW_ERR_BAD_SIZE;
    if (request.length > slot.total - slot.progress) return GTOS_WINDOW_ERR_RANGE;
    return 0;
}

int NativeWindows::Submit(unsigned ownerId, const GtosWindowSubmitRequest& request,
                          const unsigned char* bytes) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, request.handle);
    if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
    int result = ValidateSubmit(*slot, request);
    if (result) return result;
    // Whole chunk validated before any draft byte changes.
    for (unsigned at = 0; at < request.length; at += 4)
        if (bytes[at] > bytes[at + 3] || bytes[at + 1] > bytes[at + 3] ||
            bytes[at + 2] > bytes[at + 3]) return GTOS_WINDOW_ERR_BAD_PIXELS;
    CopyBytes(slot->draft + slot->progress, bytes, request.length);
    slot->progress += request.length;
    return (int)request.length;
}

int NativeWindows::Present(unsigned ownerId, const GtosWindowControlRequest& request) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, request.handle);
    if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
    if (request.version != GTOS_WINDOW_ABI_VERSION) return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
    if (slot->lifecycle != GTOS_WINDOW_LIVE) return GTOS_WINDOW_ERR_BAD_STATE;
    if (slot->progress != slot->total) return GTOS_WINDOW_ERR_BAD_STATE;
    if (slot->frameGeneration == 0xffffffffU) return GTOS_WINDOW_ERR_OVERFLOW;
    CopyBytes(slot->front, slot->draft, slot->total);
    ++slot->frameGeneration;
    ScrubDraft(*slot);
    return 0;
}

int NativeWindows::Close(unsigned ownerId, const GtosWindowControlRequest& request) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, request.handle);
    if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
    if (request.version != GTOS_WINDOW_ABI_VERSION) return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
    // Application close is destruction: the handle is revoked immediately.
    ClearSlot(*slot);
    return 0;
}

int NativeWindows::Info(unsigned ownerId, const GtosWindowInfoRequest& request,
                        GtosWindowInfoResponse& out) const {
    WindowGuard guard;
    const Slot* slot = Find(ownerId, request.handle);
    if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
    if (request.version != GTOS_WINDOW_ABI_VERSION) return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
    out.handle = slot->handle;
    out.lifecycle = slot->lifecycle;
    if (slot->progress > 0) out.show = GTOS_WINDOW_SHOW_DRAFTING;
    else if (slot->frameGeneration == 0) out.show = GTOS_WINDOW_SHOW_EMPTY;
    else out.show = GTOS_WINDOW_SHOW_PUBLISHED;
    out.width = slot->width;
    out.height = slot->height;
    out.frame_generation = slot->frameGeneration;
    out.progress = slot->progress;
    out.total = slot->total;
    out.queue_depth = slot->queueCount + (slot->resetPending ? 1U : 0U) +
                      (slot->closePending ? 1U : 0U);
    out.dropped = slot->dropped;
    return 0;
}

void NativeWindows::RequestClose(unsigned ownerId, unsigned handle) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, handle);
    if (!slot || slot->lifecycle != GTOS_WINDOW_LIVE) return;
    slot->lifecycle = GTOS_WINDOW_CLOSING;
    // Unread normal input and any pending reset are dropped; one CLOSE remains.
    DropNormal(*slot);
    slot->resetPending = false;
    slot->closePending = true;
}

void NativeWindows::ResetInput(unsigned ownerId, unsigned handle) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, handle);
    if (!slot || slot->lifecycle != GTOS_WINDOW_LIVE) return;
    DropNormal(*slot);
    slot->resetPending = true;
}

bool NativeWindows::DeliverInput(unsigned ownerId, unsigned handle,
                                 const GtosInputEvent& event) {
    WindowGuard guard;
    Slot* slot = Find(ownerId, handle);
    if (!slot || slot->lifecycle != GTOS_WINDOW_LIVE) return false;
    return PushNormal(*slot, event);
}

int NativeWindows::SelectLocked(Slot& slot, unsigned count, unsigned& selected) {
    if (!count || count > GTOS_INPUT_READ_LIMIT) return GTOS_WINDOW_ERR_BAD_SIZE;
    selected = 0;
    if (slot.lifecycle == GTOS_WINDOW_CLOSING) {
        if (slot.closePending) {
            GtosInputEvent event;
            event.type = GTOS_INPUT_CLOSE;
            event.code = 0;
            event.x = 0;
            event.y = 0;
            scratch[selected++] = event;
        }
        return 0;
    }
    if (slot.lifecycle != GTOS_WINDOW_LIVE) return GTOS_WINDOW_ERR_BAD_HANDLE;
    if (slot.resetPending && selected < count) {
        GtosInputEvent event;
        event.type = GTOS_INPUT_RESET;
        event.code = 0;
        event.x = 0;
        event.y = 0;
        scratch[selected++] = event;
    }
    for (unsigned at = 0; at < slot.queueCount && selected < count; ++at) {
        unsigned index = (slot.queueHead + at) % GTOS_INPUT_NORMAL_ENTRIES;
        scratch[selected++] = slot.queue[index];
    }
    return 0;
}

void NativeWindows::CommitLocked(Slot& slot, unsigned selected) {
    if (!selected) return;
    if (slot.lifecycle == GTOS_WINDOW_CLOSING) {
        slot.closePending = false;
        return;
    }
    if (slot.resetPending) {
        slot.resetPending = false;
        --selected;
    }
    if (selected > slot.queueCount) selected = slot.queueCount;
    slot.queueHead = (slot.queueHead + selected) % GTOS_INPUT_NORMAL_ENTRIES;
    slot.queueCount -= selected;
}

int NativeWindows::Call(unsigned ownerId, UserMemory& memory, unsigned operation,
                        unsigned address, unsigned bytes) {
    WindowGuard guard;
    if (!ownerId) return GTOS_WINDOW_ERR_BAD_STATE;
    if (operation == GTOS_SYS_WINDOW_CREATE) {
        if (bytes != GTOS_WINDOW_CREATE_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosWindowCreateRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        // Same snapshot-then-version order as every other operation.
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        return Create(ownerId, request);
    }
    if (operation == GTOS_SYS_WINDOW_SUBMIT) {
        if (bytes != GTOS_WINDOW_SUBMIT_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosWindowSubmitRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        Slot* slot = Find(ownerId, request.handle);
        if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
        int result = ValidateSubmit(*slot, request);
        if (result) return result;
        unsigned char bounce[GTOS_WINDOW_WRITE_LIMIT];
        if (!memory.CopyFromUser(bounce, request.source, request.length))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        return Submit(ownerId, request, bounce);
    }
    if (operation == GTOS_SYS_WINDOW_PRESENT) {
        if (bytes != GTOS_WINDOW_CONTROL_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosWindowControlRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        return Present(ownerId, request);
    }
    if (operation == GTOS_SYS_WINDOW_CLOSE) {
        if (bytes != GTOS_WINDOW_CONTROL_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosWindowControlRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        return Close(ownerId, request);
    }
    if (operation == GTOS_SYS_WINDOW_INFO) {
        if (bytes != GTOS_WINDOW_INFO_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosWindowInfoRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        GtosWindowInfoResponse response;
        int result = Info(ownerId, request, response);
        if (result) return result;
        if (!memory.CopyToUser(request.result_va, &response, sizeof(response)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        return 0;
    }
    if (operation == GTOS_SYS_INPUT_READ) {
        if (bytes != GTOS_INPUT_READ_REQUEST_BYTES) return GTOS_WINDOW_ERR_BAD_SIZE;
        GtosInputReadRequest request;
        if (!memory.CopyFromUser(&request, address, sizeof(request)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        // The version gate precedes any selection, copy or consumption.
        if (request.version != GTOS_WINDOW_ABI_VERSION)
            return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
        Slot* slot = Find(ownerId, request.handle);
        if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
        unsigned selected = 0;
        int result = SelectLocked(*slot, request.count, selected);
        if (result) return result;
        // Empty queue copies nothing at all.
        if (!selected) return 0;
        if (!memory.CopyToUser(request.destination, scratch,
                               selected * (unsigned)sizeof(GtosInputEvent)))
            return GTOS_WINDOW_ERR_BAD_ADDRESS;
        CommitLocked(*slot, selected);
        return (int)selected;
    }
    return GTOS_WINDOW_ERR_UNSUPPORTED_VERSION;
}

void NativeWindows::ReclaimOwner(unsigned ownerId) {
    WindowGuard guard;
    if (!ownerId) return;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].owner == ownerId && slots[i].lifecycle != GTOS_WINDOW_DEAD)
            ClearSlot(slots[i]);
}

#if defined(GTOS_NATIVE_RECLAIM)
NativeWindows::Census NativeWindows::Snapshot() const {
    WindowGuard guard;
    Census census = {0, 0, 0};
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i) {
        const Slot& slot = slots[i];
        if (slot.lifecycle == GTOS_WINDOW_DEAD) continue;
        ++census.live;
        // A draft exists only between the first SUBMIT and PRESENT.
        if (slot.progress && slot.progress < slot.total) ++census.partial;
        if (slot.queueCount || slot.resetPending || slot.closePending) ++census.pending;
    }
    return census;
}
#endif

bool NativeWindows::CopySlot(unsigned index, gui::NativeWindowView& view,
                             gui::NativeImageSnapshot& out) const {
    WindowGuard guard;
    if (index >= GTOS_WINDOW_MAX_OBJECTS) return false;
    const Slot& slot = slots[index];
    view.handle = slot.handle;
    view.lifecycle = slot.lifecycle;
    if (slot.lifecycle == GTOS_WINDOW_DEAD) {
        view.width = view.height = view.frameGeneration = 0;
        out.generation = out.width = out.height = 0;
        ZeroBytes(out.rgba, gui::NativeImageMaximumBytes);
        return true;
    }
    view.width = slot.width;
    view.height = slot.height;
    view.frameGeneration = slot.frameGeneration;
    if (slot.frameGeneration) {
        out.generation = slot.frameGeneration;
        CopyBytes(out.rgba, slot.front, GTOS_WINDOW_MAX_BYTES);
    } else {
        // Unpublished: zero frame, but the CREATE dimensions still describe the
        // window, so the desktop can compute the client rectangle.
        out.generation = 0;
        ZeroBytes(out.rgba, gui::NativeImageMaximumBytes);
    }
    out.width = slot.width;
    out.height = slot.height;
    return true;
}

bool NativeWindows::ContainsWindow(unsigned handle) const {
    WindowGuard guard;
    if (!handle) return false;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].handle == handle && slots[i].lifecycle != GTOS_WINDOW_DEAD)
            return true;
    return false;
}

unsigned NativeWindows::OwnerOfHandle(unsigned handle) const {
    WindowGuard guard;
    if (!handle) return 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].handle == handle && slots[i].lifecycle != GTOS_WINDOW_DEAD)
            return slots[i].owner;
    return 0;
}

int NativeWindows::RouteInput(unsigned handle, const GtosInputEvent& event) {
    WindowGuard guard;
    if (!handle) return GTOS_WINDOW_ERR_BAD_HANDLE;
    Slot* slot = 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].handle == handle && slots[i].lifecycle != GTOS_WINDOW_DEAD) {
            slot = &slots[i];
            break;
        }
    if (!slot) return GTOS_WINDOW_ERR_BAD_HANDLE;
    if (slot->lifecycle != GTOS_WINDOW_LIVE) return GTOS_WINDOW_ERR_BAD_STATE;
    return PushNormal(*slot, event) ? 1 : 0;
}

void NativeWindows::ResetWindowInput(unsigned handle) {
    WindowGuard guard;
    Slot* slot = 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].handle == handle && slots[i].lifecycle != GTOS_WINDOW_DEAD) {
            slot = &slots[i];
            break;
        }
    if (!slot || slot->lifecycle != GTOS_WINDOW_LIVE) return;
    DropNormal(*slot);
    slot->resetPending = true;
}

void NativeWindows::RequestWindowClose(unsigned handle) {
    WindowGuard guard;
    Slot* slot = 0;
    for (unsigned i = 0; i < GTOS_WINDOW_MAX_OBJECTS; ++i)
        if (slots[i].handle == handle && slots[i].lifecycle != GTOS_WINDOW_DEAD) {
            slot = &slots[i];
            break;
        }
    if (!slot || slot->lifecycle != GTOS_WINDOW_LIVE) return;
    slot->lifecycle = GTOS_WINDOW_CLOSING;
    DropNormal(*slot);
    slot->resetPending = false;
    slot->closePending = true;
}
} }
