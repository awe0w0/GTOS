// Copyright 2026 the GTOS project authors.
// Private IA32 page backing for the pinned Abseil low-level allocator.
#ifndef GTOS_LOW_LEVEL_PAGES_H_
#define GTOS_LOW_LEVEL_PAGES_H_

#include <stddef.h>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// These internal operations require one user task in its private GTOS address
// space. They do not implement POSIX mmap or async-signal-safe allocation.
size_t GtosLowLevelPageSize();

// Returns a fully committed RW reservation, or nullptr for an invalid request,
// quota exhaustion or allocation failure. A failed commit releases its original
// reservation before returning. Contract violations and failed rollback trap.
void* GtosLowLevelMapPages(size_t size, size_t alignment);

// Releases one or more complete original reservations that exactly cover the
// requested range. Gaps, partial reservations and repeated release return false
// before mutation. All selected kernel records are checked before any release.
// A kernel release error returns false; earlier successful releases stay removed
// from the table. The allocator caller must terminate on false, not retry as if
// the release were atomic. This API never reports a failed release as success.
bool GtosLowLevelUnmapPages(void* address, size_t size);

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // GTOS_LOW_LEVEL_PAGES_H_
