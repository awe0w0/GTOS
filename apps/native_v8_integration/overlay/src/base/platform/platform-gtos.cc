// Copyright 2026 the GTOS project authors.
// Native IA32 bindings for the pinned V8 OS interface.
#include "src/base/platform/platform.h"

#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "src/base/platform/gtos-native-api/readonly_abi.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/time.h"
#include "src/base/timezone-cache.h"
#include "tools/gtos_isolate_diagnostic/runtime/thread-context.h"

#if !V8_OS_GTOS || V8_OS_POSIX || V8_OS_LINUX || !defined(__i386__) || \
    !defined(GTOS_ISOLATE_COMPILE_DIAGNOSTIC)
#error This backend requires the independent GTOS IA32 diagnostic target
#endif

namespace v8 {
namespace base {
namespace {
// This native PC profile defines immutable UTC local time without DST.
// UTC wall time uses the separate validated CMOS/PIT realtime ABI.
class GtosUtcTimezoneCache final : public TimezoneCache {
 public:
  const char* LocalTimezone(double time_ms) override {
    return __builtin_isfinite(time_ms) ? "UTC" : "";
  }
  double DaylightSavingsOffset(double) override { return 0; }
  double LocalTimeOffset(double, bool) override { return 0; }
  void Clear(TimeZoneDetection) override {
    // There is no mutable native TZ setting to redetect in this profile.
  }
};

struct Region {
  uint32_t handle, base, length;
};
Region regions[32];
uint32_t mmap_seed = 0x6d2b79f5U;

int NativeCall(unsigned call, const void* request, unsigned bytes) {
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(request))),
                 "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(call);
}

Region* Exact(void* address) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(address);
  for (Region& region : regions) {
    if (region.handle != 0 && region.base == base) return &region;
  }
  return nullptr;
}

Region* Containing(void* address, size_t size) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(address);
  if (size == 0 || ((base | size) & (GTOS_VM_PAGE_BYTES - 1)) != 0) {
    return nullptr;
  }
  for (Region& region : regions) {
    if (region.handle != 0 && base >= region.base &&
        base - region.base < region.length &&
        size <= region.length - (base - region.base)) {
      return &region;
    }
  }
  return nullptr;
}

bool Protection(OS::MemoryPermission access, unsigned* permission) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
      *permission = GTOS_VM_NONE;
      return true;
    case OS::MemoryPermission::kRead:
      *permission = GTOS_VM_READ;
      return true;
    case OS::MemoryPermission::kReadWrite:
      *permission = GTOS_VM_READ_WRITE;
      return true;
    default:
      return false;  // ABI1 has no executable/JIT data-page contract.
  }
}

bool Range(unsigned call, void* address, size_t size, unsigned permission) {
  Region* region = Containing(address, size);
  if (region == nullptr) return false;
  gtos::CurrentProcessInfo();
  GtosVmRangeRequest request = {
      GTOS_VM_ABI_VERSION, region->handle,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - region->base),
      static_cast<unsigned>(size), permission};
  return NativeCall(call, &request, sizeof(request)) == 0;
}

void ReleaseRegion(Region* region) {
  gtos::CurrentProcessInfo();
  GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, region->handle, 0};
  if (NativeCall(GTOS_SYS_VM_RELEASE, &request, sizeof(request)) != 0) {
    gtos::UnsupportedService();
  }
  *region = {};
}

bool Inside(void* base, size_t length, void* address, size_t size) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(base);
  const uintptr_t part = reinterpret_cast<uintptr_t>(address);
  return size != 0 && part >= start && part - start < length &&
         size <= length - (part - start);
}

struct LocalKey {
  uint32_t generation;
  bool active;
};
LocalKey local_keys[32];
struct LocalValue { uint32_t generation; void* value; };
thread_local LocalValue local_values[32];
uint32_t key_lock;
class KeyGuard {
 public:
  KeyGuard() {
    gtos::CurrentNativeThreadInfo();
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(&key_lock, &expected, 1U, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      gtos::Yield();
      expected = 0;
    }
  }
  ~KeyGuard() { __atomic_store_n(&key_lock, 0U, __ATOMIC_RELEASE); }
  KeyGuard(const KeyGuard&) = delete;
  KeyGuard& operator=(const KeyGuard&) = delete;
};
constexpr uint32_t kMaximumKeyGeneration = 0x03ffffffU;

LocalKey* FindKey(Thread::LocalStorageKey key) {
  if (key <= 0) gtos::UnsupportedService();
  const uint32_t value = static_cast<uint32_t>(key);
  LocalKey* entry = &local_keys[value & 31U];
  if (!entry->active || entry->generation != (value >> 5)) {
    gtos::UnsupportedService();
  }
  return entry;
}
}  // namespace

void OS::Initialize(const char*) {
  // No POSIX loader or fake GC file mapping is initialized on this target.
  gtos::CurrentProcessInfo();
}

TimezoneCache* OS::CreateTimezoneCache() {
  gtos::CurrentNativeThreadInfo();
  return new GtosUtcTimezoneCache();
}

void OS::AdjustSchedulingParams() {
  // BSP scheduling/default priority are fixed; no parameters need adjustment.
  gtos::CurrentNativeThreadInfo();
}

int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}

int OS::VSNPrintF(char* str, int length, const char* format, va_list args) {
  if (length < 0 || format == nullptr || (length > 0 && str == nullptr)) return -1;
  const int result = vsnprintf(str, static_cast<size_t>(length), format, args);
  if (result < 0 || result >= length) {
    if (length > 0) str[length - 1] = '\0';
    return -1;
  }
  return result;
}

void OS::StrNCpy(char* dest, int length, const char* src, size_t n) {
  if (length < 0 || n > static_cast<size_t>(length) ||
      (n != 0 && (dest == nullptr || src == nullptr))) {
    gtos::UnsupportedService();
  }
  size_t i = 0;
  for (; i < n && src[i] != '\0'; ++i) dest[i] = src[i];
  for (; i < n; ++i) dest[i] = '\0';
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  // The native loader admits static ET_EXEC only and rejects dynamic linking.
  return {};
}

void OS::SignalCodeMovingGC() {
  // This native kernel has no external executable-mapping profiler event stream.
  // V8's own code-movement log remains responsible for internal profiling.
}

char OS::DirectorySeparator() { return '/'; }
bool OS::isDirectorySeparator(const char ch) {
  return ch == DirectorySeparator();
}

int OS::ActivationFrameAlignment() { return 16; }
double OS::TimeCurrentMillis() { return Time::Now().ToJsTime(); }

size_t OS::AllocatePageSize() { return GTOS_VM_PAGE_BYTES; }
size_t OS::CommitPageSize() { return GTOS_VM_PAGE_BYTES; }
bool OS::HasLazyCommits() {
  // R/RW permissions eagerly allocate real zeroed frames; reserve alone does not.
  return false;
}

void OS::SetDataReadOnly(void* address, size_t size) {
  gtos::CurrentNativeThreadInfo();
  const uintptr_t base = reinterpret_cast<uintptr_t>(address);
  if (((base | size) & (GTOS_READONLY_PAGE_BYTES - 1)) != 0) {
    gtos::UnsupportedService();
  }
  if (size == 0) return;
  GtosDataReadonlyRequest request = {
      GTOS_READONLY_ABI_VERSION, static_cast<unsigned>(base),
      static_cast<unsigned>(size)};
  if (NativeCall(GTOS_SYS_DATA_READONLY, &request, sizeof(request)) != 0) {
    gtos::UnsupportedService();
  }
}

void OS::SetRandomMmapSeed(int64_t seed) {
  mmap_seed = static_cast<uint32_t>(seed) ^
              static_cast<uint32_t>(static_cast<uint64_t>(seed) >> 32);
  if (mmap_seed == 0) mmap_seed = 0x6d2b79f5U;
}

void* OS::GetRandomMmapAddr() {
  // Reproducible optional placement hints, not entropy or qualified ASLR.
  mmap_seed ^= mmap_seed << 13;
  mmap_seed ^= mmap_seed >> 17;
  mmap_seed ^= mmap_seed << 5;
  return reinterpret_cast<void*>(0x80000000U | ((mmap_seed & 0x1ffffU) << 12));
}

void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access,
                   std::optional<SharedMemoryHandle> shared) {
  unsigned permission;
  if (shared.has_value() || !Protection(access, &permission) || size == 0 ||
      (size & (GTOS_VM_PAGE_BYTES - 1)) != 0 ||
      alignment < GTOS_VM_PAGE_BYTES || (alignment & (alignment - 1)) != 0) {
    return nullptr;
  }
  const GtosProcessInfoResult info = gtos::CurrentProcessInfo();
  if (info.maximum_regions > 32) gtos::UnsupportedService();
  Region* slot = nullptr;
  for (uint32_t i = 0; i < info.maximum_regions; ++i) {
    if (regions[i].handle == 0) {
      slot = &regions[i];
      break;
    }
  }
  if (slot == nullptr) return nullptr;
  GtosVmReserveResult result = {};
  GtosVmReserveRequest request = {
      GTOS_VM_ABI_VERSION, static_cast<unsigned>(size),
      static_cast<unsigned>(alignment),
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(hint)),
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
  int status = NativeCall(GTOS_SYS_VM_RESERVE, &request, sizeof(request));
  if (status != 0 && hint != nullptr) {
    request.hint = 0;
    status = NativeCall(GTOS_SYS_VM_RESERVE, &request, sizeof(request));
  }
  if (status != 0) return nullptr;
  if (result.version != GTOS_VM_ABI_VERSION || result.handle == 0 ||
      result.handle > 0x7fffffffU || result.page_size != GTOS_VM_PAGE_BYTES ||
      result.length != size || result.base < info.user_begin ||
      result.base >= info.user_end || size > info.user_end - result.base ||
      (result.base & (alignment - 1)) != 0) {
    gtos::UnsupportedService();
  }
  *slot = {result.handle, result.base, result.length};
  void* address = reinterpret_cast<void*>(result.base);
  if (permission != GTOS_VM_NONE &&
      !Range(GTOS_SYS_VM_SET_PERMISSIONS, address, size, permission)) {
    ReleaseRegion(slot);
    return nullptr;
  }
  return address;
}

void OS::Free(void* address, size_t size) {
  Region* region = Exact(address);
  if (region == nullptr || region->length != size) gtos::UnsupportedService();
  ReleaseRegion(region);
}

void OS::Release(void* address, size_t size) {
  Region* region = Containing(address, size);
  if (region == nullptr ||
      size != region->length -
                  (reinterpret_cast<uintptr_t>(address) - region->base)) {
    gtos::UnsupportedService();
  }
  const unsigned retained =
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - region->base);
  if (retained == 0) {
    ReleaseRegion(region);
    return;
  }
  gtos::CurrentProcessInfo();
  GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, region->handle, retained};
  if (NativeCall(GTOS_SYS_VM_TRIM, &request, sizeof(request)) != 0) {
    gtos::UnsupportedService();
  }
  region->length = retained;
}

bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  unsigned permission;
  return Protection(access, &permission) &&
         Range(GTOS_SYS_VM_SET_PERMISSIONS, address, size, permission);
}
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  return SetPermissions(address, size, access);
}
bool OS::DiscardSystemPages(void* address, size_t size) {
  return Range(GTOS_SYS_VM_DISCARD, address, size, 0);
}
bool OS::DecommitPages(void* address, size_t size) {
  return Range(GTOS_SYS_VM_DECOMMIT, address, size, 0);
}
bool OS::SealPages(void*, size_t) { return false; }
bool OS::RemapPages(const void*, size_t, void*, MemoryPermission) { return false; }

bool OS::SetMemoryRegionName(const void*, size_t, const char*) {
  return false;  // Native ABI1 has no region-name service.
}

void* OS::AllocateShared(void*, size_t, MemoryPermission,
                         SharedMemoryHandle, uint64_t) {
  // Native ABI1 cannot map a shared object. A private allocation would violate
  // the caller's aliasing/ownership contract, so report allocation failure.
  return nullptr;
}

void OS::FreeShared(void*, size_t) {
  // No shared allocation can succeed in this profile. Never release a private
  // reservation through a mismatched ownership API.
  gtos::UnsupportedService();
}

bool OS::CanReserveAddressSpace() { return true; }
std::optional<AddressSpaceReservation> OS::CreateAddressSpaceReservation(
    void* hint, size_t size, size_t alignment, MemoryPermission max_permission,
    std::optional<SharedMemoryHandle> shared) {
  unsigned permission;
  if (!Protection(max_permission, &permission) || shared.has_value()) return {};
  void* base = Allocate(hint, size, alignment, MemoryPermission::kNoAccess);
  if (base == nullptr) return {};
  return AddressSpaceReservation(base, size);
}
void OS::FreeAddressSpaceReservation(AddressSpaceReservation reservation) {
  Free(reservation.base(), reservation.size());
}
bool AddressSpaceReservation::Allocate(void* address, size_t size,
                                       OS::MemoryPermission access) {
  return Inside(base_, size_, address, size) &&
         OS::SetPermissions(address, size, access);
}
bool AddressSpaceReservation::Free(void* address, size_t size) {
  // Release backing while preserving the inaccessible parent reservation.
  return Inside(base_, size_, address, size) && OS::DecommitPages(address, size);
}
bool AddressSpaceReservation::SetPermissions(void* address, size_t size,
                                             OS::MemoryPermission access) {
  return Inside(base_, size_, address, size) &&
         OS::SetPermissions(address, size, access);
}
bool AddressSpaceReservation::RecommitPages(void* address, size_t size,
                                            OS::MemoryPermission access) {
  return Inside(base_, size_, address, size) &&
         OS::RecommitPages(address, size, access);
}
bool AddressSpaceReservation::DiscardSystemPages(void* address, size_t size) {
  return Inside(base_, size_, address, size) &&
         OS::DiscardSystemPages(address, size);
}
bool AddressSpaceReservation::DecommitPages(void* address, size_t size) {
  return Inside(base_, size_, address, size) && OS::DecommitPages(address, size);
}
std::optional<AddressSpaceReservation>
AddressSpaceReservation::CreateSubReservation(
    void* address, size_t size, OS::MemoryPermission max_permission) {
  unsigned permission;
  if (!Inside(base_, size_, address, size) ||
      !Protection(max_permission, &permission) ||
      !OS::DecommitPages(address, size)) {
    return {};
  }
  // The upstream parent RegionAllocator owns disjoint subranges. GTOS keeps
  // one real kernel reservation; the child receives its checked inaccessible
  // range and commits/decommits inside that reservation.
  return AddressSpaceReservation(address, size);
}
bool AddressSpaceReservation::FreeSubReservation(
    AddressSpaceReservation reservation) {
  return OS::DecommitPages(reservation.base(), reservation.size());
}
bool AddressSpaceReservation::AllocateShared(void*, size_t,
    OS::MemoryPermission, SharedMemoryHandle, uint64_t) {
  return false;
}
bool AddressSpaceReservation::FreeShared(void*, size_t) { return false; }

int OS::GetCurrentProcessId() {
  const uint32_t id = gtos::CurrentNativeThreadInfo().process_id;
  if (id > 0x7fffffffU) gtos::UnsupportedService();
  return static_cast<int>(id);
}
int OS::GetCurrentThreadIdInternal() {
  const uint32_t id = gtos::CurrentNativeThreadInfo().thread_id;
  if (id > 0x7fffffffU) gtos::UnsupportedService();
  return static_cast<int>(id);
}
void OS::Sleep(TimeDelta interval) {
  const int64_t duration = interval.InMicroseconds();
  if (duration <= 0) return;
  gtos::CurrentNativeThreadInfo();
  const int64_t begin = gtos_v8_clock_read_microseconds();
  for (;;) {
    gtos::Yield();
    const int64_t now = gtos_v8_clock_read_microseconds();
    if (now < begin) gtos::UnsupportedService();
    if (now - begin >= duration) return;
  }
}
void OS::Abort() { gtos::UnsupportedService(); }
void OS::DebugBreak() { gtos::UnsupportedService(); }
void OS::ExitProcess(int code) {
  unsigned call = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(static_cast<unsigned>(code)), "c"(0U) : "memory", "cc");
  gtos::UnsupportedService();
}

Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
  return static_cast<uintptr_t>(gtos::CurrentNativeThreadInfo().stack_end);
}
Stack::StackSlot Stack::ObtainCurrentThreadStackReservedLimit() {
  return static_cast<uintptr_t>(gtos::CurrentNativeThreadInfo().stack_begin);
}
Stack::StackSlot Stack::GetCommittedStackLimit() {
  return static_cast<uintptr_t>(gtos::CurrentNativeThreadInfo().stack_begin);
}
Stack::StackSlot Stack::GetCurrentStackPosition() {
  return __builtin_frame_address(0);
}
void Stack::SetCurrentThreadStackBounds(uintptr_t, uintptr_t) {
  gtos::UnsupportedService();  // The native kernel owns the fixed stack mapping.
}

Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  KeyGuard guard;
  for (uint32_t i = 0; i < 32; ++i) {
    LocalKey& key = local_keys[i];
    if (!key.active && key.generation < kMaximumKeyGeneration) {
      ++key.generation;
      key.active = true;
      return static_cast<LocalStorageKey>((key.generation << 5) | i);
    }
  }
  gtos::UnsupportedService();
}
void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  KeyGuard guard;
  LocalKey* entry = FindKey(key);
  entry->active = false;
}
void* Thread::GetThreadLocal(LocalStorageKey key) {
  KeyGuard guard;
  LocalKey* entry = FindKey(key);
  LocalValue& value = local_values[static_cast<uint32_t>(key) & 31U];
  if (value.generation != entry->generation) value = {entry->generation, nullptr};
  return value.value;
}
void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  KeyGuard guard;
  LocalKey* entry = FindKey(key);
  local_values[static_cast<uint32_t>(key) & 31U] = {entry->generation, value};
}

}  // namespace base
}  // namespace v8
