#ifndef GTOS_NATIVE_THREAD_CONTEXT_H
#define GTOS_NATIVE_THREAD_CONTEXT_H
#include <stdint.h>
#include "src/base/platform/gtos-native-api/info_abi.h"
namespace v8 { namespace base { namespace gtos {
struct NativeThreadState {
  uint32_t id;
  int error;
  void* tls_array;
  void* destructors;
  bool cleaning;
};
// No allocation or TLS access in self-query, registration or errno lookup.
// Self-query accepts only the kernel's one- or four-context BSP profiles.
GtosProcessInfoResult CurrentNativeThreadInfo();
NativeThreadState& CurrentNativeThreadState();
void ReleaseNativeThreadState(NativeThreadState&);
} } }
#endif
