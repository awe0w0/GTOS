#ifndef GTOS_V8_CLOCK_CALLS_H
#define GTOS_V8_CLOCK_CALLS_H
#include "src/base/platform/elapsed-timer.h"
static_assert(sizeof(v8::base::TimeTicks)==8 && sizeof(v8::base::TimeDelta)==8,"Actual upstream i386 hidden return objects");
extern "C" int64_t actual_v8_now();
extern "C" bool actual_v8_high_resolution();
extern "C" bool actual_v8_thread_ticks_supported();
extern "C" void actual_v8_timer_start(v8::base::ElapsedTimer*);
extern "C" void actual_v8_timer_start_at(v8::base::ElapsedTimer*,v8::base::TimeTicks);
extern "C" bool actual_v8_timer_started(const v8::base::ElapsedTimer*);
extern "C" int64_t actual_v8_timer_elapsed(const v8::base::ElapsedTimer*);
extern "C" int64_t actual_v8_timer_elapsed_at(const v8::base::ElapsedTimer*,v8::base::TimeTicks);
extern "C" int64_t actual_v8_timer_restart(v8::base::ElapsedTimer*);
extern "C" bool actual_v8_timer_expired(const v8::base::ElapsedTimer*,int64_t);
extern "C" void actual_v8_timer_stop(v8::base::ElapsedTimer*);
#endif
