/* A deliberate type confusion that must NOT compile.
 *
 * The adapter object is a user-space address; `gfx::AcceleratedWidget` is the
 * kernel's 32-bit window handle. They are different types on purpose, so neither
 * a pointer-to-integer cast nor an integer-to-pointer assignment may exist. If
 * this file ever compiles, the two have been merged into one thing and the GTOS
 * branch in `ui/gfx/native_ui_types.h` no longer says what it claims.
 *
 * It is compiled only as a negative: the test requires a non-zero result and no
 * `fatal error` in the log, so a missing header cannot be mistaken for a refused
 * conversion.
 */
#include <ui/gfx/native_ui_types.h>

extern "C" unsigned gtos_gfx_types_confusion(void *adapter) {
    gfx::AcceleratedWidget widget = static_cast<gfx::AcceleratedWidget>(adapter);
    gfx::NativeWindow window = widget;
    return window == nullptr ? 0U : widget;
}
