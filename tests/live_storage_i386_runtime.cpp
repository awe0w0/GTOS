// The freestanding 32-bit half of the live-storage unit needs one language-runtime symbol and links no C++ runtime to get it.
//
// The unit compiles storage classes with virtual member functions, so every vtable names __cxa_pure_virtual. The 64-bit half links
// the host's C++ runtime and never missed it; the 32-bit half links with -nostdlib, so the reference stayed undefined and the image
// could not be produced at all. This definition is what the rest of the repository does for its freestanding images: a pure virtual
// call is a programming error, and here it stops the machine instead of returning somewhere.
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {
    for (;;) {
    }
}
