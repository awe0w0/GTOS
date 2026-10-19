#ifndef GTOS_PROCESS_NATIVE_WINDOW_ADAPTER_H
#define GTOS_PROCESS_NATIVE_WINDOW_ADAPTER_H

// A small user-space window object for GTOS.
//
// This is the object a UI layer holds. It is NOT a kernel structure and it is
// NOT the kernel handle: `handle()` is the persistent positive integer the
// kernel returned from CREATE, while the adapter itself is an ordinary
// user-space object whose address may be published to a widget layer. The two
// are different things and must never be reinterpreted into each other.
//
// It performs exactly one thing per call: build the fixed request record from
// <process/window_abi.h> and hand it to the six-call wrapper in
// <process/window_client.h>. There is no pre-validation, no result rewriting
// and no hidden state machine beyond the lifecycle this object genuinely owns.
//
// What it deliberately does NOT do:
//   * it never creates a window implicitly. A default adapter has no window, and
//     no constructor, copy or move ever calls CREATE.
//   * it is not copyable or movable. Once its address has been published to a
//     widget layer that pointer must stay valid, so copying it would silently
//     produce a second object claiming the same kernel handle.
//   * its destructor does not CLOSE. Destruction of the C++ object and
//     destruction of the kernel window are separate events; a destructor that
//     closed would make the object's lifetime the window's lifetime, which is
//     exactly the confusion this adapter exists to avoid. Call Close().
//   * it does not allocate, throw, spawn threads or wait.
//   * it models a single window and its own root content view. GTOS has no view
//     tree, so there are no children, no embedding and no parent pointers. If a
//     caller needs a real tree, this type must not pretend to provide one.
//
// Errors are the kernel's own negative values, returned unchanged. A BAD_STATE is
// reported and never interpreted: the kernel returns it both for a window that is
// no longer LIVE and for a draft that is not complete, so on its own it cannot
// tell "the desktop is closing this window" apart from "that frame was never
// finished". Only an answer with discriminating power updates the local state: a
// successful INFO carrying the kernel's lifecycle word, or a BAD_HANDLE returned
// for a call that carried this object's handle. No other error proves the window
// is gone, and none of them clears the handle.

#include <process/window_abi.h>
#include <process/window_client.h>

namespace gtos {
namespace process {

class NativeWindowAdapter {
 public:
  NativeWindowAdapter() = default;
  NativeWindowAdapter(const NativeWindowAdapter&) = delete;
  NativeWindowAdapter& operator=(const NativeWindowAdapter&) = delete;

  // Never closes anything. See the note above.
  ~NativeWindowAdapter() = default;

  // The kernel's persistent handle, 0 when this object holds no window.
  int handle() const { return handle_; }

  // True only while the kernel handle is held. A failed CREATE, a successful
  // CLOSE and a BAD_HANDLE all leave this false.
  bool valid() const { return handle_ > 0; }

  // True once the window is known to be gone.
  bool destroyed() const { return handle_ == 0 && lifecycle_ == GTOS_WINDOW_DEAD; }

  // True while the desktop has asked to close the window but it still exists.
  bool closing() const { return closing_; }

  unsigned width() const { return width_; }
  unsigned height() const { return height_; }

  // The kernel identifier to publish as a compositor surface, 0 when there is
  // none. This is a kernel identifier, not this object's address.
  unsigned kernel_handle() const { return handle_ > 0 ? (unsigned)handle_ : 0u; }

  // The single root content view of this window. It shares this object's
  // identity rather than introducing a second one, which is the same shape some
  // other platforms use where a view handle and a window handle are the same
  // type. It is not a tree node: there are no children and no parent.
  NativeWindowAdapter* root_view() { return this; }
  const NativeWindowAdapter* root_view() const { return this; }

  // CREATE. On success stores the positive handle and returns it; on failure the
  // object is left exactly as it was (so a refusal does not destroy an existing
  // window). `stride` is width * 4 for the only supported format.
  int Create(unsigned width, unsigned height) {
    GtosWindowCreateRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.width = width;
    request.height = height;
    request.stride = width * 4u;
    request.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int result = WindowCreate(&request);
    if (result < 0) {
      return result;
    }
    handle_ = result;
    width_ = width;
    height_ = height;
    lifecycle_ = GTOS_WINDOW_LIVE;
    closing_ = false;
    return result;
  }

  // SUBMIT. `offset` is the byte offset inside the window content,
  // `pixels`/`bytes` describe one contiguous premultiplied RGBA8 chunk. Returns
  // the accepted byte count or a negative error. BAD_HANDLE means the kernel no
  // longer knows this handle, so the local claim is dropped; BAD_STATE (window
  // closing, offset not the exact next position) leaves everything as it was.
  int Submit(const void* pixels, unsigned bytes, unsigned offset) {
    GtosWindowSubmitRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = (unsigned)handle_;
    request.offset = offset;
    request.source = (unsigned)pixels;
    request.length = bytes;
    const int result = WindowSubmit(&request);
    if (Gone(result)) ForgetHandle();
    return result;
  }

  // PRESENT. 0 or a negative error. BAD_STATE here means either that the window
  // is not LIVE or that the draft is not complete; both are reported and neither
  // is read as "the desktop is closing". A closing window is observed through a
  // successful INFO, which is the only call that reports the lifecycle word.
  int Present() {
    GtosWindowControlRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = (unsigned)handle_;
    const int result = WindowPresent(&request);
    if (Gone(result)) ForgetHandle();
    return result;
  }

  // INFO. Writes a full GtosWindowInfoResponse through `out` and adopts the
  // kernel's lifecycle word, so a window that died for another reason is not
  // reported as live. `out` is only read when the call succeeded: a failed call
  // leaves the caller's buffer untouched, and adopting its stale bytes would
  // invent a state the kernel never reported.
  int Info(GtosWindowInfoResponse* out) {
    GtosWindowInfoRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = (unsigned)handle_;
    request.result_va = (unsigned)out;
    const int result = WindowInfo(&request);
    if (result == 0) {
      lifecycle_ = out->lifecycle;
      closing_ = lifecycle_ == GTOS_WINDOW_CLOSING;
      // The real bank returns BAD_HANDLE for a destroyed window - Find() fails
      // before any lifecycle word is copied - so this arm is defensive only. It
      // is not the evidence a caller should rely on for "the window is gone".
      if (lifecycle_ == GTOS_WINDOW_DEAD) ForgetHandle();
    } else if (Gone(result)) {
      ForgetHandle();
    }
    return result;
  }

  // INPUT_READ. Copies up to `capacity` fixed 16-byte records into `out`, which
  // is caller-owned storage: no adapter scratch is ever handed out, so nothing
  // can outlive the next read. Returns the number of records copied, 0 for an
  // empty queue, or a negative error. A closing window still delivers its single
  // CLOSE record; BAD_HANDLE drops the local claim.
  int ReadInput(GtosInputEvent* out, unsigned capacity) {
    GtosInputReadRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = (unsigned)handle_;
    request.count = capacity;
    request.destination = (unsigned)out;
    const int result = WindowInputRead(&request);
    if (Gone(result)) ForgetHandle();
    return result;
  }

  // CLOSE. Destroys the window. A success and a BAD_HANDLE both end the local
  // claim, through the same path: the first destroyed it, the second says it was
  // already not ours. The kernel destroys a found slot without consulting the
  // lifecycle, so it has no BAD_STATE answer here; should one ever arrive it is
  // returned untouched and the handle is kept.
  int Close() {
    GtosWindowControlRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = (unsigned)handle_;
    const int result = WindowClose(&request);
    if (result == 0 || Gone(result)) ForgetHandle();
    return result;
  }

 private:
  // The one place a local claim on the kernel handle is dropped. It is reached
  // only from an answer that really ends the claim - a successful CLOSE or a
  // BAD_HANDLE for a call that carried this handle - so it never runs on a guess.
  void ForgetHandle() {
    handle_ = 0;
    width_ = 0;
    height_ = 0;
    lifecycle_ = GTOS_WINDOW_DEAD;
    closing_ = false;
  }

  // BAD_HANDLE for a call that actually carried our handle. With no handle there
  // is nothing to forget, and the kernel's refusal is just passed back.
  bool Gone(int result) const {
    return handle_ > 0 && result == GTOS_WINDOW_ERR_BAD_HANDLE;
  }

  int handle_ = 0;
  unsigned width_ = 0;
  unsigned height_ = 0;
  unsigned lifecycle_ = GTOS_WINDOW_DEAD;
  bool closing_ = false;
};

}  // namespace process
}  // namespace gtos

#endif
