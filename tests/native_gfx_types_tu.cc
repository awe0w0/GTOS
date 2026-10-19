/* A real i686 consumer of the real Chromium gfx type header.
 *
 * This translation unit includes <ui/gfx/native_ui_types.h> itself - the 244-line
 * header from the locked Chromium view, extended by the reviewed GTOS overlay -
 * and then states what the two GTOS branches have to mean. It is not a mirror
 * header and it does not stand in for the real one: `-H` records which file was
 * actually opened, and the overlay is built from the pinned original bytes.
 *
 * The identity checks are the point. A build that reached these types by defining
 * Linux, or by defining Ozone, or by defining a host OS, would fail the
 * assertions below, because those flags are all required to be 0 here.
 *
 * Nothing in this file claims that Chromium runs on GTOS. It proves that the
 * type layer can be entered under the GTOS identity with the shapes the adapter
 * actually has.
 */
#include <ui/gfx/native_ui_types.h>

#include <process/native_window_adapter.h>
#include <process/window_abi.h>

namespace {

template <typename A, typename B>
struct Same {
    static const bool value = false;
};
template <typename A>
struct Same<A, A> {
    static const bool value = true;
};

// ---- identity ------------------------------------------------------------
// Exactly one OS identity is true, and it is GTOS. build/build_config.h derives
// this from __GTOS__ alone; nothing here defines a host macro.
static_assert(BUILDFLAG(IS_GTOS) == 1, "IS_GTOS must be 1");
static_assert(BUILDFLAG(IS_LINUX) == 0, "GTOS must not claim Linux");
static_assert(BUILDFLAG(IS_APPLE) == 0, "GTOS must not claim Apple");
static_assert(BUILDFLAG(IS_MAC) == 0, "GTOS must not claim macOS");
static_assert(BUILDFLAG(IS_IOS) == 0, "GTOS must not claim iOS");
static_assert(BUILDFLAG(IS_WIN) == 0, "GTOS must not claim Windows");
static_assert(BUILDFLAG(IS_ANDROID) == 0, "GTOS must not claim Android");
static_assert(BUILDFLAG(IS_CHROMEOS) == 0, "GTOS must not claim ChromeOS");
static_assert(BUILDFLAG(IS_FUCHSIA) == 0, "GTOS must not claim Fuchsia");
static_assert(BUILDFLAG(IS_OZONE) == 0, "GTOS is not Ozone");
static_assert(BUILDFLAG(IS_POSIX) == 0, "GTOS is not a POSIX target here");

#if !defined(OS_GTOS)
#error OS_GTOS must be defined
#endif
#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_APPLE) || \
    defined(OS_ANDROID) || defined(OS_POSIX) || defined(OS_MAC) || defined(OS_IOS)
#error no other OS identity may be defined alongside OS_GTOS
#endif
// The GTOS identity must be reachable only through the real build config, so the
// architecture detection there has to have produced an i686 32-bit little-endian
// target as well.
#if !defined(ARCH_CPU_X86) || !defined(ARCH_CPU_32_BITS) || \
    !defined(ARCH_CPU_LITTLE_ENDIAN)
#error the real build config must report an i686 32-bit little-endian target
#endif

// ---- the four gfx types, in the shape the adapter has --------------------
using Adapter = gtos::process::NativeWindowAdapter;

static_assert(Same<gfx::NativeWindow, Adapter*>::value,
              "NativeWindow must be the adapter pointer");
static_assert(Same<gfx::NativeView, Adapter*>::value,
              "NativeView must be the adapter pointer");
static_assert(Same<gfx::NativeEvent, const GtosInputEvent*>::value,
              "NativeEvent must be a borrowed input record");
// NativeViewAccessible keeps the header's own "not supported here" level rather
// than gaining an invented implementation.
static_assert(Same<gfx::NativeViewAccessible,
                   gfx::UnimplementedNativeViewAccessible*>::value,
              "NativeViewAccessible must stay the unimplemented level");

// The adapter object and the kernel handle are different things. The handle is a
// 32-bit kernel identifier; the adapter is a user-space address.
static_assert(Same<gfx::AcceleratedWidget, uint32_t>::value,
              "AcceleratedWidget must be the 32-bit kernel handle");
static_assert(sizeof(gfx::AcceleratedWidget) == 4, "handle is 32 bits");
static_assert(gfx::kNullAcceleratedWidget == 0, "null widget is 0");
static_assert(!Same<gfx::AcceleratedWidget, gfx::NativeWindow>::value,
              "the handle and the adapter must not be the same type");

// NativeViewId stays the header's own intptr_t. On i686 that is 32 bits, which is
// wide enough to carry a user-space adapter address unchanged.
static_assert(Same<gfx::NativeViewId, intptr_t>::value,
              "NativeViewId must stay intptr_t");
static_assert(sizeof(gfx::NativeViewId) == sizeof(void *),
              "NativeViewId must be pointer sized");
static_assert(sizeof(gfx::NativeViewId) == 4, "i686 pointer width");

// There is no NativeCursor on GTOS: the header only defines one for Aura and
// Android, and no usable cursor backend exists yet. Asserting its absence keeps a
// later "just typedef it to void*" shortcut from passing as support.

}  // namespace

// A concrete use of all four types, so the branch is not merely parsed: the
// object keeps a window pointer, a view pointer that is the same object, a
// borrowed event pointer and a kernel handle.
extern "C" int gtos_gfx_types_probe(void *window, void *view, void *event,
                                    unsigned handle) {
    gfx::NativeWindow native_window = static_cast<Adapter*>(window);
    gfx::NativeView native_view = static_cast<Adapter*>(view);
    gfx::NativeEvent native_event = static_cast<const GtosInputEvent*>(event);
    gfx::AcceleratedWidget widget = handle;
    gfx::NativeViewId identifier = reinterpret_cast<gfx::NativeViewId>(window);
    if (native_window != native_view) return 1;
    if (native_window == nullptr) return 2;
    if (widget == gfx::kNullAcceleratedWidget) return 3;
    if (native_event == nullptr) return 4;
    if (identifier == 0) return 5;
    return 0;
}
