#ifndef GTOS_PROCESS_NATIVE_WINDOWS_H
#define GTOS_PROCESS_NATIVE_WINDOWS_H
#include <process/window_abi.h>
#include <gui/native_image.h>
#include <gui/native_window.h>

namespace gtos {
namespace process {

// Non-owning user-memory access used by the window endpoint. The kernel adapter
// forwards to the real ProcessAddressSpace checked-copy; host tests supply their
// own storage. No virtual destructor: an instantiated kernel adapter must not
// acquire a deleting destructor that would pull in operator delete.
struct UserMemory {
    virtual bool CopyFromUser(void* destination, unsigned source, unsigned length) = 0;
    virtual bool CopyToUser(unsigned destination, const void* source, unsigned length) = 0;
protected:
    ~UserMemory() {}
};

// One bounded native window slot: trusted supervisor storage only. No mapping
// changes, no allocation, no GUI calls and no retained user addresses occur in
// this service. Lifecycle is live/closing/dead; frame data is progress/total and
// frame_generation, which is never an object identity.
class NativeWindows : public gui::NativeWindowProvider {
#ifdef GTOS_WINDOW_HOST_TEST
    friend struct NativeWindowHostAccess;
#endif
    struct Slot {
        unsigned handle, owner, lifecycle, width, height, stride, total;
        unsigned progress, frameGeneration;
        unsigned char draft[GTOS_WINDOW_MAX_BYTES];
        unsigned char front[GTOS_WINDOW_MAX_BYTES];
        GtosInputEvent queue[GTOS_INPUT_NORMAL_ENTRIES];
        unsigned queueHead, queueCount, dropped;
        bool resetPending, closePending;
    };
    Slot slots[GTOS_WINDOW_MAX_OBJECTS];
    // Bank scratch for one INPUT_READ transaction; never on the kernel stack.
    GtosInputEvent scratch[GTOS_INPUT_READ_LIMIT];
    unsigned nextHandle;
    bool available;

    Slot* Find(unsigned ownerId, unsigned handle);
    const Slot* Find(unsigned ownerId, unsigned handle) const;
    Slot* FreeSlot();
    void ClearSlot(Slot& slot);
    void ClearQueue(Slot& slot);
    void DropNormal(Slot& slot);
    // Returns true only when THIS delivery performed an overflow reset.
    bool PushNormal(Slot& slot, const GtosInputEvent& event);
    int ValidateSubmit(const Slot& slot, const GtosWindowSubmitRequest& request) const;
    void ScrubDraft(Slot& slot);
    // Locked helpers: only valid inside one Call() transaction.
    int SelectLocked(Slot& slot, unsigned count, unsigned& selected);
    void CommitLocked(Slot& slot, unsigned selected);
    NativeWindows(const NativeWindows&);
    NativeWindows& operator=(const NativeWindows&);

public:
    NativeWindows();
    static NativeWindows& Instance();
    // Boot configuration only, after a framebuffer desktop actually exists.
    void SetAvailable(bool value);

    int Create(unsigned ownerId, const GtosWindowCreateRequest& request);
    int Submit(unsigned ownerId, const GtosWindowSubmitRequest& request,
               const unsigned char* bytes);
    int Present(unsigned ownerId, const GtosWindowControlRequest& request);
    int Close(unsigned ownerId, const GtosWindowControlRequest& request);
    int Info(unsigned ownerId, const GtosWindowInfoRequest& request,
             GtosWindowInfoResponse& out) const;

    // One syscall-shaped entry: size, request snapshot, version gate, validation,
    // response copy. This is the only path an application can reach.
    int Call(unsigned ownerId, UserMemory& memory, unsigned operation,
             unsigned address, unsigned bytes);

    // Desktop request-close: idempotent, at most one CLOSE, never destroys.
    void RequestClose(unsigned ownerId, unsigned handle);
    // Ordinary overflow / focus loss / IRQ overflow: only affects a live object.
    void ResetInput(unsigned ownerId, unsigned handle);

    // Delivery from the deferred desktop path. Returns true only when THIS call
    // performed an overflow reset, so the caller can cancel a stale capture.
    bool DeliverInput(unsigned ownerId, unsigned handle, const GtosInputEvent& event);

    void ReclaimOwner(unsigned ownerId);

#if defined(GTOS_NATIVE_RECLAIM)
    // Kernel acceptance observation only, compiled into the reclaim diagnostic
    // variant and never into the production kernel. A read-only census of the
    // bank: how many slots are not DEAD, how many of those hold a partially
    // written draft, and how many hold pending input. It exists so the kernel can
    // record what a process really owned while it was still running; it is never
    // part of the syscall path and it changes no state.
    struct Census {
        unsigned live, partial, pending;
    };
    Census Snapshot() const;
#endif

    // gui::NativeWindowProvider: only an out-of-range index returns false; every
    // in-range index fully defines both outputs, including empty and unpublished.
    virtual bool CopySlot(unsigned index, gui::NativeWindowView& view,
                          gui::NativeImageSnapshot& out) const;
    virtual bool ContainsWindow(unsigned handle) const;
    virtual int RouteInput(unsigned handle, const GtosInputEvent& event);
    virtual void ResetWindowInput(unsigned handle);
    virtual void RequestWindowClose(unsigned handle);

    // Handle -> owner for one live/closing window, or 0. Trusted kernel use only.
    unsigned OwnerOfHandle(unsigned handle) const;
};
} }
#endif
