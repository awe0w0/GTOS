#include "memory_record.h"

#include <process/abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>

#include "partition_alloc/internal/page_allocator_internal.h"
#include "partition_alloc/page_allocator.h"
#include "partition_alloc/partition_alloc_base/time/time.h"
#include "partition_alloc/partition_tls.h"
#include "partition_alloc/stack/stack.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {
    1, GTOS_PARTITION_GUEST_MODE, 0, 0, 0, 0, 0};
}

namespace {
namespace pa = partition_alloc;
using Access = pa::PageAccessibilityConfiguration;
using Disposition = pa::PageAccessibilityDisposition;
constexpr size_t kPage = GTOS_VM_PAGE_BYTES;
constexpr auto kTag = pa::PageTag::kChromium;

bool Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok)
    native_memory_record.failures = native_memory_record.failures + 1;
  return ok;
}

bool Zero(uintptr_t address) {
  const auto* bytes = reinterpret_cast<const volatile unsigned char*>(address);
  for (size_t i = 0; i < kPage; ++i) {
    if (bytes[i] != 0)
      return false;
  }
  return true;
}

void Write(const char* text, size_t size) {
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80" : "+a"(call) : "b"(text), "c"(size) : "memory", "cc");
}

bool ReservationCapacity(unsigned expected) {
  GtosProcessInfoResult info{};
  const GtosProcessInfoRequest request{GTOS_PROCESS_INFO_ABI_VERSION, 0,
                                       reinterpret_cast<unsigned>(&info), sizeof(info)};
  unsigned call = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80" : "+a"(call) : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  if (!Check(call == 0 && info.version == GTOS_PROCESS_INFO_ABI_VERSION &&
             info.maximum_regions == expected))
    return false;
  constexpr Access read_write(Access::kReadWrite);
  uintptr_t reservations[128]{};
  unsigned used = 0;
  bool zeroed = true;
  for (; used < expected; ++used) {
    const uintptr_t address = pa::AllocPages(kPage, kPage, read_write, kTag);
    if (!address)
      break;
    reservations[used] = address;
    zeroed &= Zero(address);
    *reinterpret_cast<volatile unsigned char*>(address) = static_cast<unsigned char>(used + 1);
  }
  native_memory_record.base = used;
  if (!Check(used == expected)) {
    while (used)
      pa::FreePages(reservations[--used], kPage);
    return false;
  }
  Check(zeroed);
  bool distinct = true;
  for (unsigned i = 0; i < used; ++i)
    distinct &= *reinterpret_cast<const volatile unsigned char*>(reservations[i]) == i + 1;
  Check(distinct);
  const uintptr_t excess = pa::AllocPages(kPage, kPage, read_write, kTag);
  Check(excess == 0 && pa::GetAllocPageErrorCode() == -GTOS_VM_ERR_LIMIT);
  if (excess)
    pa::FreePages(excess, kPage);
  Check(pa::GetTotalMappedSize() == used * kPage);
  pa::FreePages(reservations[0], kPage);
  constexpr size_t chunk_size = 64 * kPage;
  const uintptr_t chunk = pa::AllocPages(chunk_size, chunk_size, read_write, kTag);
  if (Check(chunk && (chunk & (chunk_size - 1)) == 0)) {
    Check(Zero(chunk) && Zero(chunk + chunk_size - kPage));
    pa::FreePages(chunk, chunk_size);
  }
  for (unsigned i = 1; i < used; ++i)
    pa::FreePages(reservations[i], kPage);
  return Check(pa::GetTotalMappedSize() == 0);
}
}  // namespace

extern "C" int NativeEntry() {
  constexpr char begin[] = "PARTITION PAGES BEGIN\n";
  Write(begin, sizeof(begin) - 1);
  constexpr Access inaccessible(Access::kInaccessible);
  constexpr Access read_write(Access::kReadWrite);
  constexpr Access executable(Access::kReadExecute);
  constexpr auto required = Disposition::kRequireUpdate;
  constexpr uintptr_t hint = 0x81001000;

  if constexpr (GTOS_PARTITION_GUEST_MODE == 1) {
    const uintptr_t address = pa::AllocPages(hint, kPage, kPage, read_write, kTag);
    if (!Check(address != 0) ||
        !Check(pa::TrySetSystemPagesAccess(address, kPage, inaccessible)))
      return 2;
    native_memory_record.base = address;
    native_memory_record.fault_address = address;
    native_memory_record.stage = 1;
    *reinterpret_cast<volatile unsigned char*>(address) = 1;
    return 3;
  }
  if constexpr (GTOS_PARTITION_GUEST_MODE == 2) {
    if (!ReservationCapacity(128))
      return 1;
    native_memory_record.stage = 2;
    return native_memory_record.failures ? 1 : 0;
  }

  Check(pa::GetTotalMappedSize() == 0);
  Check(pa::internal::GetTls() == nullptr && !pa::internal::IsTombstoneSlow());
  const uintptr_t stack_top = pa::internal::GetStackTop();
  const uintptr_t stack_address = reinterpret_cast<uintptr_t>(&stack_top);
  Check(stack_top > stack_address && stack_top - stack_address <= 8192);
  using Ticks = pa::internal::base::TimeTicks;
  const Ticks before = Ticks::Now();
  Check(Ticks::GetClock() == Ticks::Clock::GTOS_CLOCK_MONOTONIC &&
        !Ticks::IsHighResolution() && Ticks::IsConsistentAcrossProcesses());
  const auto occupied = pa::internal::SystemAllocPages(hint, kPage, inaccessible, kTag);
  if (!Check(occupied == hint))
    return 4;
  Check(pa::internal::SystemAllocPages(hint, kPage, inaccessible, kTag) == 0);
  Check(pa::GetAllocPageErrorCode() == -GTOS_VM_ERR_CONFLICT);
  const auto address = pa::AllocPagesWithAlignOffset(
      hint, 2 * kPage, 16 * kPage, kPage, inaccessible, kTag);
  if (!Check(address != 0 && address != occupied && address % (16 * kPage) == kPage))
    return 5;
  Check(pa::GetTotalMappedSize() == 3 * kPage);
  Check(!pa::TrySetSystemPagesAccess(address + 1, kPage, read_write));
  Check(!pa::TrySetSystemPagesAccess(address + kPage, 2 * kPage, read_write));
  Check(!pa::TrySetSystemPagesAccess(address, kPage, executable));
  Check(!pa::SealSystemPages(address, kPage));
  if (!Check(pa::TryRecommitSystemPages(address, 2 * kPage, read_write, required)))
    return 6;
  Check(Zero(address) && Zero(address + kPage));
  auto* bytes = reinterpret_cast<volatile unsigned char*>(address);
  bytes[0] = 25;
  bytes[kPage] = 77;
  pa::DiscardSystemPages(address, kPage);
  Check(Zero(address) && bytes[kPage] == 77);
  bytes[0] = 43;
  Check(pa::DecommitAndZeroSystemPages(address, kPage));
  if (!Check(pa::TryRecommitSystemPages(address, kPage, read_write, required)))
    return 7;
  Check(Zero(address) && bytes[kPage] == 77);
  pa::FreePages(address, 2 * kPage);
  pa::FreePages(occupied, kPage);
  Check(pa::GetTotalMappedSize() == 0);
  Check(!pa::TrySetSystemPagesAccess(address, kPage, read_write));
  Check(pa::AllocPages(kPage, kPage, executable, kTag) == 0);
  Check(pa::GetAllocPageErrorCode() == -GTOS_VM_ERR_PERMISSION);
  Check(pa::ReserveAddressSpace(kPage));
  Check(!pa::ReserveAddressSpace(kPage));
  Check(pa::ReleaseReservation() && !pa::ReleaseReservation());
  Check(pa::GetTotalMappedSize() == 0);
  for (unsigned i = 0; i < 40; ++i) {
    const auto reused = pa::AllocPages(hint, kPage, kPage, read_write, kTag);
    if (!Check(reused == hint))
      return 8;
    Check(Zero(reused));
    pa::FreePages(reused, kPage);
  }
  Check(pa::GetTotalMappedSize() == 0);
  Check(Ticks::Now() >= before);
  if (!ReservationCapacity(32))
    return 9;
#if defined(GTOS_PARTITION_GUEST_WRONG_EXPECTATION)
  Check(pa::GetTotalMappedSize() == kPage);
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures ? 1 : 0;
}
