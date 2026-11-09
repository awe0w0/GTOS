// Native data-page ownership for the embedded GTOS Perfetto backend.
#include "perfetto/ext/base/paged_memory.h"

#include <stdint.h>
#include <utility>

#include <process/vm_abi.h>

#include "perfetto/base/logging.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !defined(__i386__)
#error Native paged memory requires the GTOS IA32 VM ABI
#endif

#if defined(GTOS_PERFETTO_VM_TEST)
extern "C" int GtosPerfettoVmTestCall(unsigned, const void*, unsigned);
#endif

namespace perfetto {
namespace base {
namespace {
constexpr size_t kPage = GTOS_VM_PAGE_BYTES;
// The native VM's per-request limit is smaller than the Content page budget.
constexpr size_t kCommitLimit = 256 * kPage;
static_assert(sizeof(void*) == 4 && sizeof(unsigned) == 4, "native VM wire layout");

int Call(unsigned operation, const void* request, unsigned bytes) {
#if defined(GTOS_PERFETTO_VM_TEST)
  return GtosPerfettoVmTestCall(operation, request, bytes);
#else
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(request)), "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(operation);
#endif
}

size_t Rounded(size_t bytes) { return (bytes + kPage - 1) & ~(kPage - 1); }

bool Commit(unsigned handle, size_t begin, size_t end) {
  while (begin < end) {
    const size_t remaining = end - begin;
    const size_t length = remaining < kCommitLimit ? remaining : kCommitLimit;
    const GtosVmRangeRequest request{GTOS_VM_ABI_VERSION, handle,
                                    static_cast<unsigned>(kPage + begin),
                                    static_cast<unsigned>(length), GTOS_VM_READ_WRITE};
    if (Call(GTOS_SYS_VM_SET_PERMISSIONS, &request, sizeof(request)) != 0) return false;
    begin += length;
  }
  return true;
}

int Release(unsigned handle) {
  const GtosVmControlRequest request{GTOS_VM_ABI_VERSION, handle, 0};
  return Call(GTOS_SYS_VM_RELEASE, &request, sizeof(request));
}
}

PagedMemory::PagedMemory() = default;

PagedMemory::PagedMemory(char* pointer, size_t bytes) : p_(pointer), size_(bytes) {
  ANNOTATE_NEW_BUFFER(p_, size_, committed_size_)
}

PagedMemory PagedMemory::Allocate(size_t bytes, int flags) {
  PERFETTO_CHECK((flags & ~(kMayFail | kDontCommit)) == 0);
  // Both guards and the rounded allocation must fit the unsigned wire domain.
  if (bytes == 0 || bytes > UINT32_MAX - 3 * kPage + 1) {
    if (flags & kMayFail) return PagedMemory();
    PERFETTO_FATAL("invalid native page allocation size");
  }
  const size_t rounded = Rounded(bytes);
  const size_t outer = rounded + 2 * kPage;
  GtosVmReserveResult result{};
  const GtosVmReserveRequest request{GTOS_VM_ABI_VERSION, static_cast<unsigned>(outer),
                                    static_cast<unsigned>(kPage), 0,
                                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
  if (Call(GTOS_SYS_VM_RESERVE, &request, sizeof(request)) != 0) {
    if (flags & kMayFail) return PagedMemory();
    PERFETTO_FATAL("native VM reservation failed");
  }
  // Reject a broken service contract before using any returned address.
  PERFETTO_CHECK(result.version == GTOS_VM_ABI_VERSION && result.handle != 0 &&
                 result.base != 0 && result.base % kPage == 0 &&
                 result.length == outer && result.page_size == kPage &&
                 result.base <= UINT32_MAX - outer);
  if (!(flags & kDontCommit) && !Commit(result.handle, 0, rounded)) {
    PERFETTO_CHECK(Release(result.handle) == 0);
    if (flags & kMayFail) return PagedMemory();
    PERFETTO_FATAL("native VM commit failed");
  }
  PagedMemory memory(reinterpret_cast<char*>(result.base + kPage), bytes);
  memory.gtos_handle_ = result.handle;
  if (!(flags & kDontCommit)) {
    ANNOTATE_CHANGE_SIZE(memory.p_, memory.size_, memory.committed_size_, bytes)
    memory.committed_size_ = bytes;
  }
  return memory;
}

PagedMemory::PagedMemory(PagedMemory&& other) noexcept {
  *this = other;
  other.p_ = nullptr;
  other.size_ = other.committed_size_ = other.gtos_handle_ = 0;
}

PagedMemory& PagedMemory::operator=(PagedMemory&& other) {
  if (this == &other) return *this;
  this->~PagedMemory();
  new (this) PagedMemory(std::move(other));
  return *this;
}

PagedMemory::~PagedMemory() {
  if (!p_) return;
  PERFETTO_CHECK(gtos_handle_ != 0 && size_ != 0);
  PERFETTO_CHECK(Release(gtos_handle_) == 0);
  ANNOTATE_DELETE_BUFFER(p_, size_, committed_size_)
}

void PagedMemory::EnsureCommitted(size_t bytes) {
  PERFETTO_CHECK(p_ && bytes <= size_);
  if (bytes <= committed_size_) return;
  const size_t begin = Rounded(committed_size_);
  const size_t end = Rounded(bytes);
  PERFETTO_CHECK(Commit(gtos_handle_, begin, end));
  const size_t committed = end < size_ ? end : size_;
  ANNOTATE_CHANGE_SIZE(p_, size_, committed_size_, committed)
  committed_size_ = committed;
}

bool PagedMemory::AdviseDontNeed(void* pointer, size_t bytes) {
  const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
  const uintptr_t start = reinterpret_cast<uintptr_t>(p_);
  if (!p_ || bytes == 0 || address < start || address - start > committed_size_ ||
      bytes > committed_size_ - (address - start) || address % kPage || bytes % kPage)
    return false;
  // Discard zeroes fully resident pages while keeping access permissions.
  // It does not promise to reduce the resident-page charge.
  const GtosVmRangeRequest request{GTOS_VM_ABI_VERSION, gtos_handle_,
                                  static_cast<unsigned>(kPage + address - start),
                                  static_cast<unsigned>(bytes), 0};
  return Call(GTOS_SYS_VM_DISCARD, &request, sizeof(request)) == 0;
}
}
}
