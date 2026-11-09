#include "perfetto/ext/base/paged_memory.h"
#include "perfetto/base/logging.h"
#include <process/vm_abi.h>
#include <stdint.h>
#include <utility>

namespace {
constexpr unsigned kCommitBytes = 256 * 4096;
alignas(4096) unsigned char pages[2][kCommitBytes + 4 * 4096];
struct Reservation { bool active; unsigned length, committed; } regions[2];
unsigned calls, reserves, commits, releases, discards, checks, failures;
int reserve_error, commit_error, release_error, discard_error;
unsigned commit_error_offset;
bool malformed;
void Write(const char* text) {
  unsigned length = 0;
  while (text[length]) ++length;
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(text), "d"(length) : "memory", "cc");
}
void Check(bool value) {
  ++checks;
  if (!value) { ++failures; Write("CHECK_FAILED\n"); }
}
unsigned Active() { return unsigned(regions[0].active) + unsigned(regions[1].active); }
}

extern "C" {
int* __llvm_libc_errno() noexcept { static int value; return &value; }
char* strerror(int) noexcept { return const_cast<char*>("test errno"); }
void* memset(void* pointer, int byte, unsigned size) {
  for (unsigned index = 0; index < size; ++index) static_cast<unsigned char*>(pointer)[index] = static_cast<unsigned char>(byte);
  return pointer;
}
void* memcpy(void* pointer, const void* source, unsigned size) {
  for (unsigned index = 0; index < size; ++index) static_cast<unsigned char*>(pointer)[index] = static_cast<const unsigned char*>(source)[index];
  return pointer;
}

// Only the kernel endpoint is injected. Requests must preserve both guard
// pages, the native handle and existing committed contents.
int GtosPerfettoVmTestCall(unsigned operation, const void* data, unsigned bytes) {
  ++calls;
  if (operation == GTOS_SYS_VM_RESERVE) {
    ++reserves;
    const auto& request = *static_cast<const GtosVmReserveRequest*>(data);
    Check(bytes == sizeof(request) && request.version == 1 && request.hint == 0);
    Check(request.alignment == 4096 && request.length % 4096 == 0 && request.length >= 12288);
    if (reserve_error) return reserve_error;
    if (Active() == 2) return GTOS_VM_ERR_LIMIT;
    const unsigned slot = regions[0].active ? 1 : 0;
    auto& region = regions[slot];
    Check(request.length <= sizeof(pages[slot]));
    region = {true, request.length, 0};
    memset(pages[slot], 0xA5, sizeof(pages[slot]));
    *reinterpret_cast<GtosVmReserveResult*>(request.result) = {
        malformed ? 0U : 1U, slot + 1, reinterpret_cast<unsigned>(pages[slot]), request.length, 4096};
    return 0;
  }
  if (operation == GTOS_SYS_VM_RELEASE) {
    ++releases;
    const auto& request = *static_cast<const GtosVmControlRequest*>(data);
    Check(bytes == sizeof(request) && request.version == 1 && request.length == 0);
    Check(request.handle >= 1 && request.handle <= 2 && regions[request.handle - 1].active);
    if (release_error) return release_error;
    regions[request.handle - 1] = {};
    return 0;
  }
  Check(operation == GTOS_SYS_VM_SET_PERMISSIONS || operation == GTOS_SYS_VM_DISCARD);
  const auto& request = *static_cast<const GtosVmRangeRequest*>(data);
  Check(bytes == sizeof(request) && request.version == 1 && request.handle >= 1 && request.handle <= 2);
  auto& region = regions[request.handle - 1];
  Check(region.active && request.offset >= 4096 && request.offset % 4096 == 0);
  Check(request.length != 0 && request.length % 4096 == 0 && request.offset + request.length <= region.length - 4096);
  if (operation == GTOS_SYS_VM_SET_PERMISSIONS) {
    ++commits;
    Check(request.protection == GTOS_VM_READ_WRITE && request.offset == 4096 + region.committed);
    if (request.length > kCommitBytes) return GTOS_VM_ERR_LIMIT;
    if (commit_error && (!commit_error_offset || request.offset == commit_error_offset))
      return commit_error;
    region.committed += request.length;
  } else {
    ++discards;
    Check(request.protection == 0 && request.offset + request.length <= 4096 + region.committed);
    if (discard_error) return discard_error;
  }
  memset(pages[request.handle - 1] + request.offset, 0, request.length);
  return 0;
}
}

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("INVALID_VM_REQUEST\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int MemoryTestMain() {
  using perfetto::base::PagedMemory;
#if GTOS_MEMORY_CASE != 0
  Write("INVALID_REQUEST\n");
#if GTOS_MEMORY_CASE == 1
  auto memory = PagedMemory::Allocate(1, 0x40);
#elif GTOS_MEMORY_CASE == 2
  auto memory = PagedMemory::Allocate(1);
  memory.EnsureCommitted(2);
#elif GTOS_MEMORY_CASE == 3
  malformed = true;
  auto memory = PagedMemory::Allocate(1);
#elif GTOS_MEMORY_CASE == 4
  reserve_error = GTOS_VM_ERR_NO_MEMORY;
  auto memory = PagedMemory::Allocate(1);
#elif GTOS_MEMORY_CASE == 5
  commit_error = GTOS_VM_ERR_NO_MEMORY;
  auto memory = PagedMemory::Allocate(1);
#elif GTOS_MEMORY_CASE == 6
  { auto memory = PagedMemory::Allocate(1); release_error = -22; }
#endif
  Write("RETURNED_UNEXPECTED\n");
  return 8;
#else
  { PagedMemory empty; Check(!empty.IsValid() && empty.size() == 0); Check(!empty.AdviseDontNeed(nullptr, 4096)); }
  const unsigned before = calls;
  Check(!PagedMemory::Allocate(0, PagedMemory::kMayFail).IsValid());
  Check(!PagedMemory::Allocate(0xFFFFFFFFU, PagedMemory::kMayFail).IsValid());
  Check(calls == before);
  reserve_error = GTOS_VM_ERR_LIMIT;
  Check(!PagedMemory::Allocate(1, PagedMemory::kMayFail).IsValid() && Active() == 0);
  reserve_error = 0;
  commit_error = GTOS_VM_ERR_NO_MEMORY;
  Check(!PagedMemory::Allocate(1, PagedMemory::kMayFail).IsValid() && Active() == 0 && releases == 1);
  commit_error = 0;
  const unsigned sizes[] = {1, 4095, 4096, 4097, 8192};
  for (unsigned size : sizes) {
    auto memory = PagedMemory::Allocate(size);
    auto* pointer = static_cast<unsigned char*>(memory.Get());
    Check(memory.IsValid() && memory.size() == size && reinterpret_cast<uintptr_t>(pointer) % 4096 == 0);
    Check(pointer[0] == 0 && pointer[size - 1] == 0);
    Check(pages[0][0] == 0xA5 && pages[0][regions[0].length - 1] == 0xA5);
  }
  Check(Active() == 0);
  {
    auto first = PagedMemory::Allocate(8192);
    auto second = PagedMemory::Allocate(4096);
    Check(first.Get() != second.Get() && Active() == 2);
    Check(!PagedMemory::Allocate(1, PagedMemory::kMayFail).IsValid() && Active() == 2);
    auto moved = std::move(first);
    Check(moved.IsValid() && !first.IsValid() && first.size() == 0 && Active() == 2);
    second = std::move(moved);
    Check(second.size() == 8192 && !moved.IsValid() && Active() == 1);
    PagedMemory& alias = second;
    second = std::move(alias);
    Check(second.IsValid() && second.size() == 8192 && Active() == 1);
  }
  Check(Active() == 0);
  {
    auto memory = PagedMemory::Allocate(12291, PagedMemory::kDontCommit);
    const unsigned old_commits = commits;
    memory.EnsureCommitted(0);
    Check(commits == old_commits && regions[0].committed == 0);
    auto* pointer = static_cast<unsigned char*>(memory.Get());
    Check(!memory.AdviseDontNeed(pointer, 4096));
    memory.EnsureCommitted(1);
    Check(regions[0].committed == 4096 && pointer[0] == 0 && pointer[4095] == 0);
    pointer[0] = 77;
    memory.EnsureCommitted(2);
    Check(commits == old_commits + 1 && pointer[0] == 77);
    memory.EnsureCommitted(4097);
    Check(regions[0].committed == 8192 && pointer[0] == 77 && pointer[4096] == 0);
    Check(memory.AdviseDontNeed(pointer, 4096) && pointer[0] == 0 && Active() == 1);
    pointer[0] = 88;
    discard_error = -22;
    Check(!memory.AdviseDontNeed(pointer, 4096) && pointer[0] == 88);
    discard_error = 0;
    const unsigned old_calls = calls;
    Check(!memory.AdviseDontNeed(pointer + 1, 4096));
    Check(!memory.AdviseDontNeed(pointer, 4095));
    Check(!memory.AdviseDontNeed(pointer - 4096, 4096));
    Check(!memory.AdviseDontNeed(pointer, 12288));
    Check(!memory.AdviseDontNeed(pointer, 0) && calls == old_calls);
    memory.EnsureCommitted(memory.size());
    Check(regions[0].committed == 16384 && pointer[0] == 88 && pointer[12290] == 0);
    Check(pages[0][regions[0].length - 1] == 0xA5);
  }
  Check(Active() == 0);
  const unsigned old_commits = commits;
  const unsigned old_releases = releases;
  commit_error = GTOS_VM_ERR_NO_MEMORY;
  commit_error_offset = 4096 + kCommitBytes;
  Check(!PagedMemory::Allocate(kCommitBytes + 1, PagedMemory::kMayFail).IsValid());
  Check(Active() == 0 && commits == old_commits + 2 && releases == old_releases + 1);
  commit_error = 0;
  commit_error_offset = 0;
  {
    auto memory = PagedMemory::Allocate(kCommitBytes + 4097, PagedMemory::kDontCommit);
    auto* pointer = static_cast<unsigned char*>(memory.Get());
    const unsigned before = commits;
    memory.EnsureCommitted(1);
    pointer[0] = 44;
    memory.EnsureCommitted(memory.size());
    Check(commits == before + 3 && regions[0].committed == kCommitBytes + 8192);
    Check(pointer[0] == 44 && pointer[kCommitBytes] == 0 && pointer[memory.size() - 1] == 0);
    Check(pages[0][0] == 0xA5 && pages[0][regions[0].length - 1] == 0xA5);
  }
  Check(Active() == 0);
#if defined(GTOS_MEMORY_WRONG_EXPECTATION)
  Check(Active() == 1);
#endif
  Write(failures ? "MEMORY_CHECK_FAIL\n" : "MEMORY_CHECK_PASS\n");
  return failures ? 1 : 0;
#endif
}
