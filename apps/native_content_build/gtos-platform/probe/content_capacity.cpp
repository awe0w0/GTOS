#include "memory_record.h"

#include <process/info_abi.h>
#include <process/vm_abi.h>

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

constexpr unsigned kPage = GTOS_VM_PAGE_BYTES;
constexpr unsigned kStaticPages = 8193;
constexpr unsigned kResourcePages = 12288;
constexpr unsigned kContentRegions = 128;
constexpr unsigned kChunk = 256 * kPage;

__attribute__((section(".bss.content_capacity"), aligned(kPage)))
volatile unsigned char static_data[kStaticPages * kPage];

bool Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok)
    native_memory_record.failures = native_memory_record.failures + 1;
  return ok;
}

template <class Request>
int Call(unsigned call, const Request& request) {
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(&request), "c"(sizeof(request))
               : "memory", "cc");
  return static_cast<int>(call);
}

bool ExercisePages(volatile unsigned char* data, unsigned pages) {
  for (unsigned i = 0; i < pages; ++i) {
    if (data[i * kPage])
      return false;
    data[i * kPage] = static_cast<unsigned char>(i % 251 + 1);
  }
  for (unsigned i = 0; i < pages; ++i)
    if (data[i * kPage] != i % 251 + 1)
      return false;
  return true;
}

}  // namespace

extern "C" int NativeEntry() {
  Check(ExercisePages(static_data, kStaticPages));
  GtosProcessInfoResult info{};
  const GtosProcessInfoRequest process{GTOS_PROCESS_INFO_ABI_VERSION, 0,
      reinterpret_cast<unsigned>(&info), sizeof(info)};
  Check(Call(GTOS_SYS_PROCESS_INFO, process) == 0 &&
        info.version == GTOS_PROCESS_INFO_ABI_VERSION && info.page_bytes == kPage &&
        info.maximum_pages > kStaticPages + kResourcePages &&
        info.maximum_regions == kContentRegions);
  native_memory_record.base = info.maximum_pages;

  GtosVmReserveResult region{};
  const unsigned bytes = kResourcePages * kPage;
  const GtosVmReserveRequest reserve{GTOS_VM_ABI_VERSION, bytes, kPage, 0,
      reinterpret_cast<unsigned>(&region)};
  if (!Check(Call(GTOS_SYS_VM_RESERVE, reserve) == 0 && region.length == bytes))
    return 2;
  // Keep the small reservations live alongside the main resource reservation.
  // None commit data pages. The caller's output must survive a rejected reserve.
  GtosVmReserveResult small{};
  const GtosVmReserveRequest small_request{GTOS_VM_ABI_VERSION, kPage, kPage, 0,
      reinterpret_cast<unsigned>(&small)};
  for (unsigned i = 1; i < kContentRegions; ++i) {
    if (!Check(Call(GTOS_SYS_VM_RESERVE, small_request) == 0 &&
               small.handle == region.handle + i &&
               small.base == region.base + bytes + (i - 1) * kPage))
      return 4;
  }
  const unsigned last_handle = small.handle;
  const unsigned last_base = small.base;
  Check(Call(GTOS_SYS_VM_RESERVE, small_request) == GTOS_VM_ERR_LIMIT &&
        small.handle == last_handle && small.base == last_base);
  GtosVmRangeRequest range{GTOS_VM_ABI_VERSION, region.handle, 0, kChunk + kPage, GTOS_VM_READ_WRITE};
  Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, range) == GTOS_VM_ERR_LIMIT);
  range.length = kChunk;
  bool committed = true;
  for (; range.offset < bytes; range.offset += range.length) {
    if (Call(GTOS_SYS_VM_SET_PERMISSIONS, range) != 0) {
      committed = false;
      break;
    }
  }
  if (!Check(committed))
    return 3;
  Check(ExercisePages(reinterpret_cast<volatile unsigned char*>(region.base), kResourcePages));
  GtosVmRegionInfo resident{};
  const GtosVmQueryRequest query{GTOS_VM_ABI_VERSION, region.handle,
      reinterpret_cast<unsigned>(&resident)};
  Check(Call(GTOS_SYS_VM_QUERY, query) == 0 && resident.base == region.base &&
        resident.length == bytes && resident.resident_pages == kResourcePages);
  native_memory_record.fault_address = resident.resident_pages;
#if defined(GTOS_CONTENT_CAPACITY_GUEST_WRONG_EXPECTATION)
  Check(resident.resident_pages == kResourcePages - 1);
#else
  Check(resident.resident_pages == kResourcePages);
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}
