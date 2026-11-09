#include "memory_record.h"
#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/base/logging.h"
#include <stdint.h>
#include <utility>

#ifndef GTOS_MEMORY_GUEST_MODE
#define GTOS_MEMORY_GUEST_MODE 0
#endif
#if GTOS_MEMORY_GUEST_MODE < 0 || GTOS_MEMORY_GUEST_MODE > 4
#error Unknown native paged memory consumer
#endif

extern "C" volatile gtos_perfetto_probe::Record native_memory_record
    __attribute__((section(".data.vm_record"), used, aligned(4))) = {
        1, GTOS_MEMORY_GUEST_MODE, 0, 0, 0, 0, 0};

namespace {
void Write(const char* text) {
  unsigned size = 0;
  while (text[size]) ++size;
  unsigned operation = 0x4701;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(text), "c"(size) : "memory", "cc");
}
void Check(bool value) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!value) native_memory_record.failures = native_memory_record.failures + 1;
}
bool Zero(const void* pointer, unsigned size) {
  const volatile unsigned char* value = static_cast<const volatile unsigned char*>(pointer);
  for (unsigned index = 0; index < size; ++index) if (value[index]) return false;
  return true;
}
}

// A logging dependency for the native library, not an alternative VM provider.
extern "C" char* strerror(int) noexcept { return const_cast<char*>("native page request"); }
namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("PMEM ASSERT\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int NativeEntry() {
  using perfetto::base::PagedMemory;
  Write("PMEM CONSUMER BEGIN\n");
#if GTOS_MEMORY_GUEST_MODE == 0
  for (unsigned iteration = 0; iteration < 16; ++iteration) {
    auto memory = PagedMemory::Allocate(8193, PagedMemory::kDontCommit);
    auto* pointer = static_cast<unsigned char*>(memory.Get());
    Check(memory.IsValid() && reinterpret_cast<uintptr_t>(pointer) % 4096 == 0);
    memory.EnsureCommitted(1);
    Check(Zero(pointer, 4096));
    pointer[0] = 77;
    memory.EnsureCommitted(4097);
    Check(pointer[0] == 77 && Zero(pointer + 4096, 4096));
    Check(memory.AdviseDontNeed(pointer, 4096) && Zero(pointer, 4096));
    memory.EnsureCommitted(memory.size());
    Check(Zero(pointer + 8192, 4096));
    pointer[8192] = 66;
    auto moved = std::move(memory);
    Check(!memory.IsValid() && moved.IsValid() && moved.size() == 8193);
    auto other = PagedMemory::Allocate(4096);
    other = std::move(moved);
    Check(!moved.IsValid() && static_cast<unsigned char*>(other.Get())[8192] == 66);
  }
  Check(!PagedMemory::Allocate(0, PagedMemory::kMayFail).IsValid());
  Check(!PagedMemory::Allocate(0xFFFFFFFFU, PagedMemory::kMayFail).IsValid());
  constexpr unsigned kCommitBytes = 256 * 4096;
  constexpr unsigned kLarge = kCommitBytes + 4097;
  // The observer provisions 512 runtime pages. This fails after committing
  // two chunks and must leave enough capacity for both subsequent allocations.
  Check(!PagedMemory::Allocate(2 * kCommitBytes + 1, PagedMemory::kMayFail).IsValid());
  {
    auto memory = PagedMemory::Allocate(kLarge, PagedMemory::kMayFail);
    Check(memory.IsValid());
    if (memory.IsValid()) {
      auto* pointer = static_cast<unsigned char*>(memory.Get());
      Check(Zero(pointer, kLarge));
      pointer[0] = 77;
      pointer[kCommitBytes] = 66;
      Check(memory.AdviseDontNeed(pointer + kCommitBytes, 4096) &&
            pointer[0] == 77 && Zero(pointer + kCommitBytes, 4096));
    }
  }
  {
    auto memory = PagedMemory::Allocate(kLarge, PagedMemory::kDontCommit);
    Check(memory.IsValid());
    auto* pointer = static_cast<unsigned char*>(memory.Get());
    memory.EnsureCommitted(1);
    pointer[0] = 55;
    memory.EnsureCommitted(kLarge);
    Check(pointer[0] == 55 && Zero(pointer + 4096, kLarge - 4096));
  }
#if defined(GTOS_MEMORY_GUEST_WRONG_EXPECTATION)
  Check(false);
#endif
  native_memory_record.stage = 2;
  Write(native_memory_record.failures ? "PMEM CONSUMER FAIL\n" : "PMEM CONSUMER PASS\n");
  return native_memory_record.failures ? 1 : 0;
#else
  unsigned address;
  {
    auto memory = PagedMemory::Allocate(4097,
        GTOS_MEMORY_GUEST_MODE == 3 ? PagedMemory::kDontCommit : 0);
    native_memory_record.base = reinterpret_cast<unsigned>(memory.Get());
    address = native_memory_record.base;
    if (GTOS_MEMORY_GUEST_MODE == 1) address -= 4096;
    if (GTOS_MEMORY_GUEST_MODE == 2) address += 8192;
    native_memory_record.fault_address = address;
    native_memory_record.stage = 1;
    if (GTOS_MEMORY_GUEST_MODE != 4) {
      if (GTOS_MEMORY_GUEST_MODE == 3)
        return *reinterpret_cast<volatile unsigned char*>(address);
      *reinterpret_cast<volatile unsigned char*>(address) = 42;
    }
  }
  // Mode 4 accesses the released reservation, after the real destructor.
  *reinterpret_cast<volatile unsigned char*>(address) = 42;
  Write("PMEM FAULT RETURNED UNEXPECTED\n");
  return 9;
#endif
}
