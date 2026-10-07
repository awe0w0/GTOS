#ifndef GTOS_GUI_NATIVE_IMAGE_H
#define GTOS_GUI_NATIVE_IMAGE_H

// Host-friendly fixed snapshot contract. RGBA8 premultiplied, tightly packed.
// No stdint/common-type dependency: host GUI tests and the kernel share it.
namespace gtos { namespace gui {
enum { NativeImageMaximumBytes = 4096 };
struct NativeImageSnapshot {
    unsigned int generation, width, height;
    unsigned char rgba[NativeImageMaximumBytes];
};
class NativeImageProvider {
public:
    // False leaves out untouched. Implementations own snapshot serialization;
    // the desktop may render its own fixed copy after this call returns.
    virtual bool CopyLatest(unsigned int knownGeneration, NativeImageSnapshot& out) const = 0;
};
static_assert(sizeof(unsigned int) == 4, "Native image snapshot requires 32-bit unsigned");
static_assert(sizeof(NativeImageSnapshot) == 4108, "Native image snapshot layout");
} }
#endif
