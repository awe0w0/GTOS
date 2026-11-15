#ifndef GTOS_DRIVERS_KEYMAP_H
#define GTOS_DRIVERS_KEYMAP_H
#include <common/types.h>
namespace gtos {
namespace drivers {
enum KeyModifier {
    KeyShift = 1,
    KeyControl = 2,
    KeyAlt = 4,
    KeyCapsLock = 8,
};

enum NavigationKey {
    KeyF5 = 0x85,
    KeyHome,
    KeyEnd,
    KeyPageUp,
    KeyPageDown,
};

// PS/2 set-1 translation with a stable make/break identity per physical key.
class Set1Keymap {
    uint8_t pressed[256];
    bool extended, leftShift, rightShift, leftControl, rightControl, leftAlt, rightAlt;
    bool capsLock, capsDown;
    uint8_t pauseBytes;

  public:
    Set1Keymap();
    bool Feed(uint8_t scanCode, uint8_t &key, bool &down);
    uint8_t Modifiers() const;
};
} // namespace drivers
} // namespace gtos
#endif
