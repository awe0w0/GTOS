#ifndef GTOS_DRIVERS_KEYMAP_H
#define GTOS_DRIVERS_KEYMAP_H
#include <common/types.h>
namespace gtos {
namespace drivers {
// PS/2 set-1 translation with a stable make/break identity per physical key.
class Set1Keymap {
    uint8_t pressed[256];
    bool extended, leftShift, rightShift, capsLock, capsDown;
    uint8_t pauseBytes;

  public:
    Set1Keymap();
    bool Feed(uint8_t scanCode, uint8_t &key, bool &down);
};
} // namespace drivers
} // namespace gtos
#endif
