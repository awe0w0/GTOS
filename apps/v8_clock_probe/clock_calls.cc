#include "clock_calls.h"
#define ACTUAL __attribute__((noinline))
extern "C" ACTUAL int64_t actual_v8_now(){return v8::base::TimeTicks::Now().ToInternalValue();}
extern "C" ACTUAL bool actual_v8_high_resolution(){return v8::base::TimeTicks::IsHighResolution();}
extern "C" ACTUAL bool actual_v8_thread_ticks_supported(){return v8::base::ThreadTicks::IsSupported();}
extern "C" ACTUAL void actual_v8_timer_start(v8::base::ElapsedTimer* timer){timer->Start();}
extern "C" ACTUAL void actual_v8_timer_start_at(v8::base::ElapsedTimer* timer,v8::base::TimeTicks now){timer->Start(now);}
extern "C" ACTUAL bool actual_v8_timer_started(const v8::base::ElapsedTimer* timer){return timer->IsStarted();}
extern "C" ACTUAL int64_t actual_v8_timer_elapsed(const v8::base::ElapsedTimer* timer){return timer->Elapsed().InMicroseconds();}
extern "C" ACTUAL int64_t actual_v8_timer_elapsed_at(const v8::base::ElapsedTimer* timer,v8::base::TimeTicks now){return timer->Elapsed(now).InMicroseconds();}
extern "C" ACTUAL int64_t actual_v8_timer_restart(v8::base::ElapsedTimer* timer){return timer->Restart().InMicroseconds();}
extern "C" ACTUAL bool actual_v8_timer_expired(const v8::base::ElapsedTimer* timer,int64_t elapsed){return timer->HasExpired(v8::base::TimeDelta::FromMicroseconds(elapsed));}
extern "C" ACTUAL void actual_v8_timer_stop(v8::base::ElapsedTimer* timer){timer->Stop();}
