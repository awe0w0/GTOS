#ifndef GTOS_V8_THREAD_ID_CALLS_H
#define GTOS_V8_THREAD_ID_CALLS_H
#include "src/execution/thread-id.h"
v8::internal::ThreadId actual_v8_try();
v8::internal::ThreadId actual_v8_current();
v8::internal::ThreadId actual_v8_invalid();
v8::internal::ThreadId actual_v8_from_integer(int value);
bool actual_v8_valid(const v8::internal::ThreadId& value);
bool actual_v8_equal(const v8::internal::ThreadId& first, const v8::internal::ThreadId& second);
bool actual_v8_unequal(const v8::internal::ThreadId& first, const v8::internal::ThreadId& second);
int actual_v8_integer(const v8::internal::ThreadId& value);
static_assert(sizeof(v8::internal::ThreadId) == 4, "Actual pinned ThreadId ABI");
#endif
