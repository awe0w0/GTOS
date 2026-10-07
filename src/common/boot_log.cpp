#include <common/boot_log.h>
#include <memory/criticalsection.h>
using namespace gtos::common;
namespace {
char retained[BootLog::Capacity];
uint32_t first = 0, used = 0, overwritten = 0, newlines = 0;
static_assert((BootLog::Capacity & (BootLog::Capacity - 1)) == 0, "ring capacity");
uint32_t Position(uint32_t logical) {
    return (first + logical) & (BootLog::Capacity - 1);
}
uint32_t LineCount() {
    return newlines + (used && retained[Position(used - 1)] != '\n' ? 1 : 0);
}
}
void BootLog::Reset() {
    memory::InterruptGuard guard;
    first = used = overwritten = newlines = 0;
}
void BootLog::Put(char character) {
    if (!character) return;
    // Kernel printf already holds the BSP InterruptGuard across each message.
    // Direct callers must provide the same BSP serialization.
    if (used == Capacity) {
        if (retained[first] == '\n') --newlines;
        first = (first + 1) & (Capacity - 1);
        --used;
        if (overwritten != 0xFFFFFFFFU) ++overwritten;
    }
    retained[Position(used++)] = character;
    if (character == '\n') ++newlines;
}
uint32_t BootLog::Bytes() {
    memory::InterruptGuard guard;
    return used;
}
uint32_t BootLog::Dropped() {
    memory::InterruptGuard guard;
    return overwritten;
}
uint32_t BootLog::Lines() {
    memory::InterruptGuard guard;
    return LineCount();
}
bool BootLog::ReadLine(uint32_t index, char* destination, uint32_t capacity) {
    memory::InterruptGuard guard;
    if (!destination || !capacity) return false;
    destination[0] = 0;
    if (index >= LineCount()) return false;
    uint32_t at = 0, line = 0;
    while (at < used && line < index) {
        if (retained[Position(at)] == '\n') ++line;
        ++at;
    }
    uint32_t copied = 0;
    while (at < used && retained[Position(at)] != '\n' && copied < capacity - 1)
        destination[copied++] = retained[Position(at++)];
    destination[copied] = 0;
    return true;
}
