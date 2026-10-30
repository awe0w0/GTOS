// What a freestanding 32-bit unit needs when the classes it exercises have virtual member functions, and it links no C++ runtime.
//
// The units compile production classes whose methods are virtual, so every vtable names __cxa_pure_virtual. A 64-bit half that links
// the host's C++ runtime never misses it; a 32-bit half that links with -nostdlib gets the reference from nowhere, so the image cannot
// be produced at all. Two units hit this in the same way - live storage first, then the surface service - so the definition lives here
// once. It is what the rest of the repository does for its freestanding images: a pure virtual call is a programming error, and here
// it stops the machine instead of returning somewhere.
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {
    for (;;) {
    }
}
