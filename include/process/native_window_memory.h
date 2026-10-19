#ifndef GTOS_PROCESS_NATIVE_WINDOW_MEMORY_H
#define GTOS_PROCESS_NATIVE_WINDOW_MEMORY_H
#include <process/native_windows.h>
#include <memory/process_address_space.h>

namespace gtos {
namespace process {

// Non-owning adapter: the window endpoint forwards user copies to the same
// production ProcessAddressSpace checked-copy the rest of the native ABI uses.
// It simulates no page table, keeps no user address and adds no copy policy.
class WindowAddressSpace : public UserMemory {
    memory::ProcessAddressSpace& space;
    WindowAddressSpace(const WindowAddressSpace&);
    WindowAddressSpace& operator=(const WindowAddressSpace&);
public:
    explicit WindowAddressSpace(memory::ProcessAddressSpace& target) : space(target) {}
    virtual bool CopyFromUser(void* destination, unsigned source, unsigned length) {
        return space.CopyFromUser(destination, source, length);
    }
    virtual bool CopyToUser(unsigned destination, const void* source, unsigned length) {
        return space.CopyToUser(destination, source, length);
    }
};
} }
#endif
