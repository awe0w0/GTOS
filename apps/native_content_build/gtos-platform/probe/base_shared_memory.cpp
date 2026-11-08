#include "memory_record.h"

#include <stdlib.h>
#include <utility>
#include <process/shared_memory_abi.h>
#include <process/system_memory_abi.h>

#include "base/memory/platform_shared_memory_handle.h"
#include "base/memory/platform_shared_memory_mapper.h"
#include "base/process/process_metrics.h"
#include "mojo/core/platform_handle_utils.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {
    1, GTOS_BASE_SHARED_GUEST_MODE, 0, 0, 0, 0, 0};
[[noreturn]] void __cxa_pure_virtual() { abort(); }
}

namespace {

constexpr unsigned kPage = GTOS_SHARED_MEMORY_PAGE_BYTES;
constexpr unsigned kVersion = GTOS_SHARED_MEMORY_ABI_VERSION;

template<class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}

bool Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
  return ok;
}

int Create(unsigned bytes, unsigned mode) {
  return Call(GTOS_SYS_SHARED_MEMORY_CREATE,
              GtosSharedMemoryCreateRequest{kVersion, bytes, mode});
}

}  // namespace

extern "C" int NativeEntry() {
  base::PlatformSharedMemoryMapper mapper;
  if (GTOS_BASE_SHARED_GUEST_MODE == 1) {
    const int created = Create(kPage, GTOS_SHARED_MEMORY_UNSAFE);
    if (!Check(created > 0)) {
      return 2;
    }
    auto readonly = mapper.Map(static_cast<unsigned>(created), false, 0, kPage);
    if (!Check(readonly.has_value() && (*readonly)[0] == 0)) {
      return 2;
    }
    volatile uint8_t* bytes = readonly->data();
    native_memory_record.fault_address = reinterpret_cast<unsigned>(bytes);
    native_memory_record.stage = 1;
    *bytes = 0xcc;
    return 3;
  }

  base::SystemMemoryInfo before;
  Check(base::GetSystemMemoryInfo(&before) && before.free <= before.total &&
        before.total.InBytes() >= 8U * 1024 * 1024);
  GtosSystemMemoryInfo rejected{55, 0, 0, 0};
  Check(Call(GTOS_SYS_SYSTEM_MEMORY_INFO, GtosSystemMemoryRequest{
             0, reinterpret_cast<unsigned>(&rejected)}) == GTOS_SYSTEM_MEMORY_ERR_VERSION &&
        rejected.version == 55);
  Check(Create(0, GTOS_SHARED_MEMORY_WRITABLE) == GTOS_SHARED_MEMORY_ERR_INVALID);
  Check(Create(1, GTOS_SHARED_MEMORY_READ_ONLY) == GTOS_SHARED_MEMORY_ERR_INVALID);
  Check(Create(GTOS_SHARED_MEMORY_MAX_BYTES + 1, GTOS_SHARED_MEMORY_WRITABLE) ==
        GTOS_SHARED_MEMORY_ERR_INVALID);
  const int created = Create(kPage + 37, GTOS_SHARED_MEMORY_WRITABLE);
  if (!Check(created > 0)) {
    return 2;
  }
  base::subtle::ScopedPlatformSharedMemoryHandle writable(static_cast<unsigned>(created));
  base::SystemMemoryInfo allocated;
  Check(base::GetSystemMemoryInfo(&allocated) && allocated.total == before.total &&
        allocated.free.InBytes() + 2 * kPage == before.free.InBytes());
  mojo::PlatformHandle platform, extra;
  mojo::core::ExtractPlatformHandlesFromSharedMemoryRegionHandle(
      std::move(writable), &platform, &extra);
  Check(!writable.is_valid() && platform.is_gtos_shared_memory() && !extra.is_valid());
  Check(!platform.TakePlatformFile().is_valid() && platform.is_valid());
  Check(!platform.Clone().is_valid());
  MojoPlatformHandle wire{sizeof(wire), MOJO_PLATFORM_HANDLE_TYPE_INVALID, 0};
  mojo::PlatformHandle::ToMojoPlatformHandle(std::move(platform), &wire);
  Check(!platform.is_valid() && wire.type == MOJO_PLATFORM_HANDLE_TYPE_GTOS_SHARED_MEMORY &&
        wire.value == static_cast<unsigned>(created));
  MojoPlatformHandle wrong_type = wire;
  wrong_type.type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
  Check(!mojo::PlatformHandle::FromMojoPlatformHandle(&wrong_type).is_valid());
  writable = mojo::core::CreateSharedMemoryRegionHandleFromPlatformHandles(
      mojo::PlatformHandle::FromMojoPlatformHandle(&wire), {});
  if (!Check(writable.is_valid() && writable.get() == static_cast<unsigned>(created))) {
    return 2;
  }
  const GtosSharedMemoryControlRequest control{kVersion, writable.get()};
  Check(Call(GTOS_SYS_SHARED_MEMORY_DUP, control) == GTOS_SHARED_MEMORY_ERR_PERMISSION);
  Check(!mapper.Map(writable.get(), true, 1, 1));
  Check(!mapper.Map(writable.get(), true, kPage, 38));
  Check(Call(GTOS_SYS_SHARED_MEMORY_MAP,
             GtosSharedMemoryMapRequest{kVersion, writable.get(), 0, 1, 1, 0}) ==
        GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS);
  auto original = mapper.Map(writable.get(), true, 0, kPage + 37);
  if (!Check(original.has_value())) {
    return 2;
  }
  Check((*original)[0] == 0 && (*original)[kPage + 36] == 0);
  (*original)[0] = 0x41;
  (*original)[kPage + 3] = 0x73;
  Check(Call(GTOS_SYS_SHARED_MEMORY_CONVERT,
             GtosSharedMemoryConvertRequest{kVersion, writable.get(), GTOS_SHARED_MEMORY_READ_ONLY}) == 0);
  Check(!mapper.Map(writable.get(), true, 0, 1));
  GtosSharedMemoryInfo info{};
  Check(Call(GTOS_SYS_SHARED_MEMORY_INFO, GtosSharedMemoryInfoRequest{kVersion,
             writable.get(), reinterpret_cast<unsigned>(&info)}) == 0 &&
        info.version == kVersion && info.bytes == kPage + 37 &&
        info.mode == GTOS_SHARED_MEMORY_READ_ONLY);
  platform = mojo::PlatformHandle(std::move(writable));
  auto duplicate = platform.Clone();
  auto rejected_primary = platform.Clone();
  auto rejected_extra = platform.Clone();
  Check(rejected_primary.is_valid() && rejected_extra.is_valid());
  Check(!mojo::core::CreateSharedMemoryRegionHandleFromPlatformHandles(
             std::move(rejected_primary), std::move(rejected_extra)).is_valid());
  auto readonly = duplicate.TakeGtosSharedMemory();
  if (!Check(readonly.is_valid() && readonly.get() != static_cast<unsigned>(created))) {
    return 2;
  }
  auto alias = mapper.Map(readonly.get(), false, kPage, 37);
  if (!Check(alias.has_value())) {
    return 2;
  }
  Check(alias->data() != original->data() && (*alias)[3] == 0x73);
  platform.reset();
  readonly.reset();
  Check(Call(GTOS_SYS_SHARED_MEMORY_CLOSE, control) == GTOS_SHARED_MEMORY_ERR_BAD_HANDLE);
  (*original)[kPage + 3] = 0x5a;
  Check((*alias)[3] == 0x5a);
#if defined(GTOS_BASE_SHARED_GUEST_WRONG_EXPECTATION)
  Check((*original)[0] == 0x42);
#else
  Check((*original)[0] == 0x41);
#endif
  mapper.Unmap(alias->subspan<3>());
  mapper.Unmap(*original);
  Check(Call(GTOS_SYS_SHARED_MEMORY_UNMAP, GtosSharedMemoryUnmapRequest{
             kVersion, reinterpret_cast<unsigned>(original->data()), kPage + 37}) ==
        GTOS_SHARED_MEMORY_ERR_BAD_HANDLE);
  base::SystemMemoryInfo released;
  Check(base::GetSystemMemoryInfo(&released) && released.free == before.free &&
        released.total == before.total);
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}
