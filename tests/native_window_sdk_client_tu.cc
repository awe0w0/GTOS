/* Consumer translation unit for the exported window SDK overlay.
 *
 * Compiled as freestanding i686 C++ against the overlay plus the original SDK
 * only - never against the repository include tree - so `-H` output proves
 * where every <process/...> header really came from. It calls all six wrapper
 * functions, so the object file contains six real `int $0x80` instructions and
 * the exported opcodes can be read back out of the machine code instead of
 * being taken on trust from the header text.
 *
 * The request pointers arrive as arguments, so nothing can be constant-folded
 * away and each call has to be emitted.
 */
#include <process/abi.h>
#include <process/native_window_adapter.h>
#include <process/window_client.h>
#include <process/vm_abi.h>

namespace {

template <typename A, typename B>
struct Same {
    static const bool value = false;
};
template <typename A>
struct Same<A, A> {
    static const bool value = true;
};

// The six return types are part of the contract. A wrapper that narrowed a
// handle to 8 or 16 bits, or turned an errno into a bool, would fail here
// rather than at run time.
static_assert(Same<decltype(&gtos::process::WindowCreate),
                   int (*)(const GtosWindowCreateRequest *, unsigned)>::value,
              "WindowCreate signature");
static_assert(Same<decltype(&gtos::process::WindowSubmit),
                   int (*)(const GtosWindowSubmitRequest *, unsigned)>::value,
              "WindowSubmit signature");
static_assert(Same<decltype(&gtos::process::WindowPresent),
                   int (*)(const GtosWindowControlRequest *, unsigned)>::value,
              "WindowPresent signature");
static_assert(Same<decltype(&gtos::process::WindowClose),
                   int (*)(const GtosWindowControlRequest *, unsigned)>::value,
              "WindowClose signature");
static_assert(Same<decltype(&gtos::process::WindowInfo),
                   int (*)(const GtosWindowInfoRequest *, unsigned)>::value,
              "WindowInfo signature");
static_assert(Same<decltype(&gtos::process::WindowInputRead),
                   int (*)(const GtosInputReadRequest *, unsigned)>::value,
              "WindowInputRead signature");

// The base ABI must still resolve: these two come from the original SDK.
static_assert(GTOS_NATIVE_ABI_VERSION >= 1, "base ABI present");
static_assert(GTOS_VM_PAGE_BYTES == 4096U, "vm page bytes");

// The adapter is a plain user-space object: it holds no reference member, no
// virtual table and no padding tricks, so its size is the state it declares and
// its address is stable for as long as the caller keeps it alive.
using Adapter = gtos::process::NativeWindowAdapter;
static_assert(!__is_polymorphic(Adapter), "the adapter must have no vtable");
static_assert(!__is_empty(Adapter), "the adapter must carry real state");
// It must not be copyable or movable: publishing its address to a widget layer
// is only safe while exactly one object owns that identity.
static_assert(!__is_constructible(Adapter, const Adapter &),
              "the adapter must not be copy-constructible");
static_assert(!__is_assignable(Adapter &, const Adapter &),
              "the adapter must not be copy-assignable");

}  // namespace

// Sum of the six results. A negative error is preserved as a negative int, so
// the caller can see the kernel's number rather than a collapsed status.
extern "C" int gtos_window_sdk_client_probe(const GtosWindowCreateRequest *create,
                                            const GtosWindowSubmitRequest *submit,
                                            const GtosWindowControlRequest *present,
                                            const GtosWindowControlRequest *close,
                                            const GtosWindowInfoRequest *info,
                                            const GtosInputReadRequest *read) {
    int total = 0;
    total += gtos::process::WindowCreate(create);
    total += gtos::process::WindowSubmit(submit);
    total += gtos::process::WindowPresent(present);
    total += gtos::process::WindowClose(close);
    total += gtos::process::WindowInfo(info);
    total += gtos::process::WindowInputRead(read);
    return total;
}
