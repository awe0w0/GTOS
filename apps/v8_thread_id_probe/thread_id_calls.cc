#include "thread_id_calls.h"
#define GTOS_CALL __attribute__((noinline))
GTOS_CALL v8::internal::ThreadId actual_v8_try() { return v8::internal::ThreadId::TryGetCurrent(); }
GTOS_CALL v8::internal::ThreadId actual_v8_current() { return v8::internal::ThreadId::Current(); }
GTOS_CALL v8::internal::ThreadId actual_v8_invalid() { return v8::internal::ThreadId::Invalid(); }
GTOS_CALL v8::internal::ThreadId actual_v8_from_integer(int value) { return v8::internal::ThreadId::FromInteger(value); }
GTOS_CALL bool actual_v8_valid(const v8::internal::ThreadId& value) { return value.IsValid(); }
GTOS_CALL bool actual_v8_equal(const v8::internal::ThreadId& first, const v8::internal::ThreadId& second) { return first == second; }
GTOS_CALL bool actual_v8_unequal(const v8::internal::ThreadId& first, const v8::internal::ThreadId& second) { return first != second; }
GTOS_CALL int actual_v8_integer(const v8::internal::ThreadId& value) { return value.ToInteger(); }
