#include "api.h"
#include <stdint.h>
#include <string>
#include <string_view>
#include <process/vm_abi.h>

#include "base/files/file_path.h"
#include "base/metrics/persistent_memory_allocator.h"

namespace {
constexpr unsigned kPage = GTOS_VM_PAGE_BYTES;
constexpr unsigned kPmaBytes = 1024 * 1024;
constexpr unsigned kTypeA = 0x47500101;
constexpr unsigned kTypeB = 0x47500202;
volatile NativeClassRecord* observation;

#define REQUIRE_CLASS(condition)                                      \
  do {                                                                \
    observation->checks = observation->checks + 1;                     \
    if (!(condition)) {                                               \
      observation->error = __LINE__;                                  \
      return __LINE__;                                                \
    }                                                                 \
  } while (false)

template <typename Request>
int Call(unsigned operation, Request& request) {
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(&request)),
                 "c"(sizeof(request))
               : "memory", "cc");
  return static_cast<int>(operation);
}

int Query(unsigned handle, GtosVmRegionInfo& result) {
  GtosVmQueryRequest request = {
      GTOS_VM_ABI_VERSION, handle, reinterpret_cast<uintptr_t>(&result)};
  return Call(GTOS_SYS_VM_QUERY, request);
}

unsigned StringCases() {
  GtosVmReserveResult region = {};
  GtosVmReserveRequest reserve = {
      GTOS_VM_ABI_VERSION, 3 * kPage, kPage, 0,
      reinterpret_cast<uintptr_t>(&region)};
  REQUIRE_CLASS(Call(GTOS_SYS_VM_RESERVE, reserve) == 0);
  REQUIRE_CLASS(region.version == GTOS_VM_ABI_VERSION && region.handle &&
                region.length == 3 * kPage && region.page_size == kPage);
  GtosVmRangeRequest commit = {
      GTOS_VM_ABI_VERSION, region.handle, kPage, kPage, GTOS_VM_READ_WRITE};
  REQUIRE_CLASS(Call(GTOS_SYS_VM_SET_PERMISSIONS, commit) == 0);
  auto* bytes = reinterpret_cast<char*>(region.base + kPage);
  for (unsigned index = 0; index < kPage; ++index) bytes[index] = '\x9b';
  // Nonnull unmapped guard address and max0 must not touch any byte.
  REQUIRE_CLASS(NativeClassLength(reinterpret_cast<const char*>(region.base), 0)
                == 0);
  observation->string_cases = observation->string_cases + 1;
  for (unsigned offset : {0U, 1U, 3U, 15U, 31U, 127U, 4095U}) {
    const unsigned maximum = kPage - offset;
    REQUIRE_CLASS(NativeClassLength(bytes + offset, maximum) == maximum);
    observation->string_cases = observation->string_cases + 1;
  }
  bytes[kPage - 1] = 0;
  REQUIRE_CLASS(NativeClassLength(bytes, kPage) == kPage - 1);
  REQUIRE_CLASS(NativeClassLength(bytes + kPage - 1, 1) == 0);
  bytes[3] = 0;
  REQUIRE_CLASS(NativeClassLength(bytes, 2) == 2);
  REQUIRE_CLASS(NativeClassLength(bytes, 3) == 3);
  REQUIRE_CLASS(NativeClassLength(bytes, kPage) == 3);
  bytes[0] = 0;
  REQUIRE_CLASS(NativeClassLength(bytes, 1) == 0);
  observation->string_cases = observation->string_cases + 6;
  GtosVmControlRequest release = {GTOS_VM_ABI_VERSION, region.handle, 0};
  REQUIRE_CLASS(Call(GTOS_SYS_VM_RELEASE, release) == 0);
  GtosVmRegionInfo gone = {};
  REQUIRE_CLASS(Query(region.handle, gone) != 0);
  return 0;
}

unsigned PathCases() {
  struct Case { const char* path; const char* directory; const char* base; };
  // These lexical outcomes are copied from the pinned upstream DirName and
  // BaseName tables, without inventing filesystem normalization.
  constexpr Case cases[] = {
      {"", ".", ""}, {"aa", ".", "aa"}, {"/aa/bb", "/aa", "bb"},
      {"/aa/bb/", "/aa", "bb"}, {"aa//bb//", "aa", "bb"},
      {"/", "/", "/"}, {"//", "//", "//"}, {"///", "/", "/"},
      {"//aa/bb", "//aa", "bb"}, {"aa/", ".", "aa"}};
  for (const Case& test : cases) {
    const base::FilePath path(test.path);
    REQUIRE_CLASS(path.DirName().value() == test.directory &&
                  path.BaseName().value() == test.base &&
                  path.AsUTF8Unsafe() == test.path);
    observation->path_cases = observation->path_cases + 1;
  }
  const base::FilePath parent("aa/../bb");
  REQUIRE_CLASS(parent.ReferencesParent() && parent.value() == "aa/../bb");
  REQUIRE_CLASS(!base::FilePath("aa/..hidden").ReferencesParent());
  REQUIRE_CLASS(base::FilePath("aa\\bb").BaseName().value() == "aa\\bb");
  const auto components = base::FilePath("/aa/bb").GetComponents();
  REQUIRE_CLASS(components.size() == 3 && components[0] == "/" &&
                components[1] == "aa" && components[2] == "bb");
  REQUIRE_CLASS(base::FilePath("/aa").IsAbsolute() &&
                !base::FilePath("aa").IsAbsolute());
  REQUIRE_CLASS(base::FilePath("/aa").Append("bb").value() == "/aa/bb");
  REQUIRE_CLASS(base::FilePath("file.txt").Extension() == ".txt" &&
                base::FilePath("file.txt").RemoveExtension().value() == "file");
  REQUIRE_CLASS(base::FilePath(std::string_view("a\0b", 3)).value() == "a");
  const std::string utf8 = "\xe4\xbd\xa0\xe5\xa5\xbd\xf0\x9f\x98\x80";
  const std::u16string utf16 = u"\u4f60\u597d\U0001f600";
  const base::FilePath unicode(utf8);
  REQUIRE_CLASS(unicode.AsUTF8Unsafe() == utf8 &&
                unicode.AsUTF16Unsafe() == utf16 &&
                unicode.LossyDisplayName() == utf16 &&
                base::FilePath::FromUTF16Unsafe(utf16).value() == utf8);
  REQUIRE_CLASS(base::FilePath(std::string(1, '\x80')).AsUTF16Unsafe() ==
                std::u16string(1, u'\ufffd'));
  REQUIRE_CLASS(base::FilePath::FromUTF16Unsafe(
                    std::u16string(1, static_cast<char16_t>(0xd800)))
                    .value() == "\xef\xbf\xbd");
  observation->path_cases = observation->path_cases + 11;
  for (unsigned wave = 0; wave < 32; ++wave) {
    const std::string text(2048 + wave, 'q');
    base::FilePath original(text);
    base::FilePath copy(original);
    base::FilePath moved(static_cast<base::FilePath&&>(copy));
    REQUIRE_CLASS(moved.value() == text &&
                  original.Append("leaf").BaseName().value() == "leaf");
    observation->lifetime_waves = observation->lifetime_waves + 1;
  }
  return 0;
}

unsigned PmaCases() {
  unsigned handle = 0;
  {
    base::LocalPersistentMemoryAllocator allocator(
        kPmaBytes, UINT64_C(0x1122334455667788), "GTOS-PMA");
    REQUIRE_CLASS(allocator.Id() == UINT64_C(0x1122334455667788) &&
                  allocator.Name() == "GTOS-PMA" && allocator.size() == kPmaBytes &&
                  allocator.length() == kPmaBytes &&
                  allocator.page_size() == kPmaBytes &&
                  !allocator.IsReadonly() && !allocator.IsCorrupt());
    const uintptr_t address = reinterpret_cast<uintptr_t>(allocator.data());
    REQUIRE_CLASS(address >= 0x40000000U + kPage && !(address & (kPage - 1)));
    struct Prefix { unsigned magic, handle, requested, reserved; };
    static_assert(sizeof(Prefix) == 16);
    const Prefix prefix = *reinterpret_cast<const Prefix*>(address - kPage);
    REQUIRE_CLASS(prefix.magic == 0x504d4156 && prefix.handle &&
                  prefix.requested == kPmaBytes &&
                  prefix.reserved == kPmaBytes + kPage);
    GtosVmRegionInfo mapping = {};
    REQUIRE_CLASS(Query(prefix.handle, mapping) == 0 &&
                  mapping.version == GTOS_VM_ABI_VERSION &&
                  mapping.handle == prefix.handle && mapping.base == address - kPage &&
                  mapping.length == kPmaBytes + kPage && mapping.resident_pages == 257);
    handle = prefix.handle;
    observation->mapping_handle = handle;
    observation->mapping_base = mapping.base;
    observation->mapping_pages = mapping.resident_pages;
    const size_t initial = allocator.used();
    size_t allocated_a = 0, allocated_b = 0;
    const auto first = allocator.Allocate(4 * sizeof(uint32_t), kTypeA, &allocated_a);
    const auto second = allocator.Allocate(8 * sizeof(uint32_t), kTypeB, &allocated_b);
    REQUIRE_CLASS(first && second && first != second && allocated_a >= 16 &&
                  allocated_b >= 32 && !(allocated_a & 7) && !(allocated_b & 7) &&
                  allocator.used() > initial && allocator.used() <= kPmaBytes);
    auto* a = allocator.GetAsArray<uint32_t>(first, kTypeA, 4);
    auto* b = allocator.GetAsArray<uint32_t>(second, kTypeB, 8);
    REQUIRE_CLASS(a && b && !a[0] && !a[3] && !b[0] && !b[7]);
    for (unsigned index = 0; index < 4; ++index) a[index] = 0x12340000 + index;
    for (unsigned index = 0; index < 8; ++index) b[index] = 0x56780000 + index;
    REQUIRE_CLASS(allocator.GetAsReference(a, kTypeA) == first &&
                  allocator.GetType(first) == kTypeA &&
                  !allocator.GetAsArray<uint32_t>(first, kTypeB, 4) &&
                  !allocator.GetAsArray<uint8_t>(first, kTypeA, allocated_a + 1) &&
                  !allocator.GetAsArray<uint32_t>(0, kTypeA, 4));
    allocator.MakeIterable(first);
    allocator.MakeIterable(second);
    allocator.MakeIterable(first);
    base::PersistentMemoryAllocator::Iterator iterator(&allocator);
    uint32_t type = 0;
    size_t iter_size = 0;
    REQUIRE_CLASS(iterator.GetNext(&type, &iter_size) == first && type == kTypeA &&
                  iter_size == allocated_a && iterator.GetLast() == first);
    REQUIRE_CLASS(iterator.GetNext(&type) == second && type == kTypeB &&
                  iterator.GetNext(&type) == 0);
    iterator.Reset(first);
    REQUIRE_CLASS(iterator.GetLast() == first &&
                  iterator.GetNextOfType(kTypeB) == second &&
                  iterator.GetNextOfType(kTypeB) == 0);
    REQUIRE_CLASS(!allocator.ChangeType(second, kTypeA, kTypeA, true) &&
                  b[0] == 0x56780000 && allocator.GetType(second) == kTypeB);
    REQUIRE_CLASS(allocator.ChangeType(second, kTypeA, kTypeB, true) &&
                  allocator.GetType(second) == kTypeA &&
                  !allocator.GetAsArray<uint32_t>(second, kTypeB, 8));
    const auto* cleared = allocator.GetAsArray<uint8_t>(second, kTypeA, allocated_b);
    REQUIRE_CLASS(cleared);
    for (size_t index = 0; index < allocated_b; ++index) REQUIRE_CLASS(!cleared[index]);
    iterator.Reset();
    REQUIRE_CLASS(iterator.GetNextOfType(kTypeA) == first &&
                  iterator.GetNextOfType(kTypeA) == second &&
                  iterator.GetNextOfType(kTypeA) == 0);
    unsigned allocations = 0;
    while (allocations < 1024 && allocator.Allocate(4096, kTypeB)) ++allocations;
    REQUIRE_CLASS(allocations > 0 && allocations < 1024 &&
                  allocator.IsFull() && !allocator.IsCorrupt());
    base::PersistentMemoryAllocator::MemoryInfo info = {};
    allocator.GetMemoryInfo(&info);
    REQUIRE_CLASS(info.total == kPmaBytes && info.free < 4096);
    observation->pma_allocations = allocations + 2;
  }
  GtosVmRegionInfo gone = {};
  REQUIRE_CLASS(handle && Query(handle, gone) != 0);
  observation->mapping_released = 1;
  return 0;
}
}  // namespace

extern "C" unsigned NativeBaseClassChecks(volatile NativeClassRecord* record) {
  observation = record;
  const unsigned string_result = StringCases();
  if (string_result) return string_result;
  observation->mask = observation->mask | 1;
  const unsigned path_result = PathCases();
  if (path_result) return path_result;
  observation->mask = observation->mask | 2;
  const unsigned pma_result = PmaCases();
  if (pma_result) return pma_result;
  observation->mask = observation->mask | 4;
  return 0;
}
