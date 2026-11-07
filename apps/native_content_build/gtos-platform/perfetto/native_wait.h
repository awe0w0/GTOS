// Native task-runner watches refer exclusively to EVENT_WAIT handles.
#ifndef PERFETTO_EXT_BASE_NATIVE_WAIT_GTOS_H_
#define PERFETTO_EXT_BASE_NATIVE_WAIT_GTOS_H_
#include "perfetto/base/platform_handle.h"
#include <stddef.h>

namespace perfetto {
namespace base {
// Returns the ready array index, or count on timeout. At most sixteen handles
// (including the runner's wakeup event) can be watched. -1 means no deadline.
// File, socket and window handle namespaces are not accepted by this API.
size_t WaitForNativeEvents(const PlatformHandle*, size_t count, int timeout_ms);
}  // namespace base
}  // namespace perfetto
#endif
