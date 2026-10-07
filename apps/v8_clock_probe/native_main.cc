#include "probe.h"
#include "clock_calls.h"
extern "C" __attribute__((section(".data.clock_record"),used)) volatile NativeClockRecord native_clock_record={
    1U,1U,GTOS_CLOCK_PROBE_MODE,0,0,0,0,0,0,0,0,{},{},0,0,0,0,0,0,0,0,0,0,0,0};
extern "C" [[noreturn]] void gtos_v8_clock_fatal() {
    static const char message[]="GTOS V8 CLOCK FATAL V1\n";
    clock_probe_call(GTOS_SYS_WRITE,reinterpret_cast<unsigned>(message),sizeof(message)-1);
    clock_probe_fail(0x49000001U);
}
static void Require(bool condition,unsigned number){
    if(!condition)clock_probe_fail(0x49010000U|number);
    native_clock_record.checks=native_clock_record.checks+1;
}
static void KeepPages(){
    GtosVmReserveResult result={};
    GtosVmReserveRequest request={1,8192,4096,0,reinterpret_cast<unsigned>(&result)};
    Require(clock_probe_call(GTOS_SYS_VM_RESERVE,reinterpret_cast<unsigned>(&request),sizeof(request))==0,1);
    Require(result.version==1 && result.handle && result.base>=0x80000000U && result.length==8192 && result.page_size==4096,2);
    GtosVmRangeRequest range={1,result.handle,0,8192,GTOS_VM_READ_WRITE};
    Require(clock_probe_call(GTOS_SYS_VM_SET_PERMISSIONS,reinterpret_cast<unsigned>(&range),sizeof(range))==0,3);
    volatile unsigned char* bytes=reinterpret_cast<volatile unsigned char*>(result.base);
    for(unsigned i=0;i<8192;++i){Require(bytes[i]==0,4);bytes[i]=static_cast<unsigned char>(i^(i<4096?0x37U:0xa9U));}
    GtosVmRegionInfo info={};
    GtosVmQueryRequest query={1,result.handle,reinterpret_cast<unsigned>(&info)};
    Require(clock_probe_call(GTOS_SYS_VM_QUERY,reinterpret_cast<unsigned>(&query),sizeof(query))==0,5);
    Require(info.version==1 && info.handle==result.handle && info.base==result.base && info.length==8192 && info.resident_pages==2,6);
    native_clock_record.keep_base=result.base;native_clock_record.keep_handle=result.handle;native_clock_record.keep_pages=2;
}
static void DecodeChecks(){
    GtosClockReadResult value;
    const volatile unsigned char* input=reinterpret_cast<const volatile unsigned char*>(&native_clock_record.first);
    unsigned char* output=reinterpret_cast<unsigned char*>(&value);
    for(unsigned i=0;i<sizeof(value);++i)output[i]=input[i];
    Require(gtos_v8_clock_validate(&value),7);Require(!gtos_v8_clock_validate(nullptr),8);
    unsigned* fields[]={&value.version,&value.clock_id,&value.unit,&value.source,
        &value.capabilities,&value.resolution_us,&value.pit_input_hz,&value.pit_divisor};
    for(unsigned i=0;i<8;++i){unsigned old=*fields[i];*fields[i]^=0x80000000U;Require(!gtos_v8_clock_validate(&value),9+i);*fields[i]=old;}
    value.microseconds=0;Require(gtos_v8_clock_validate(&value),17);
    value.microseconds=0x7ffffffffffffffdULL;Require(gtos_v8_clock_validate(&value),18);
    value.microseconds=0x7ffffffffffffffeULL;Require(!gtos_v8_clock_validate(&value),19);
    value.microseconds=0x7fffffffffffffffULL;Require(!gtos_v8_clock_validate(&value),20);
    value.microseconds=0x8000000000000000ULL;Require(!gtos_v8_clock_validate(&value),21);
    value.microseconds=0xffffffffffffffffULL;Require(!gtos_v8_clock_validate(&value),22);
}
extern "C" int clock_guest_main(){
    native_clock_record.legacy_first=static_cast<unsigned>(clock_probe_call(GTOS_SYS_TICKS,0,0));
    KeepPages();
    v8::base::TimeTicks first=v8::base::TimeTicks::FromInternalValue(actual_v8_now());
    Require(!first.IsNull() && !first.IsMax() && !first.IsMin() && first.ToInternalValue()>0,23);
    Require(static_cast<unsigned long long>(first.ToInternalValue())==native_clock_record.last.microseconds+1,24);
    Require(!actual_v8_high_resolution(),25);Require(!actual_v8_thread_ticks_supported(),26);
    DecodeChecks();
    v8::base::ElapsedTimer whole;
    actual_v8_timer_start_at(&whole,first);
    v8::base::ElapsedTimer timer;
    Require(!actual_v8_timer_started(&timer),27);
    actual_v8_timer_start_at(&timer,first);Require(actual_v8_timer_started(&timer),28);
    Require(actual_v8_timer_elapsed_at(&timer,first)==0,29);
    actual_v8_timer_stop(&timer);Require(!actual_v8_timer_started(&timer),30);
    actual_v8_timer_start(&timer);unsigned long long start=native_clock_record.last.microseconds;
    int64_t elapsed=actual_v8_timer_elapsed(&timer);
    Require(elapsed>=0 && static_cast<unsigned long long>(elapsed)==native_clock_record.last.microseconds-start,31);
    Require(actual_v8_timer_expired(&timer,0),32);
    unsigned before=static_cast<unsigned>(clock_probe_call(GTOS_SYS_TICKS,0,0));
    unsigned attempts=0;
    do{
        clock_probe_call(GTOS_SYS_YIELD,0,0);elapsed=actual_v8_timer_elapsed(&timer);++attempts;
        Require(static_cast<unsigned>(clock_probe_call(GTOS_SYS_TICKS,0,0))-before<200U && attempts<100000U,33);
    }while(elapsed<20000);
    Require(static_cast<unsigned long long>(elapsed)==native_clock_record.last.microseconds-start,34);
    elapsed=actual_v8_timer_restart(&timer);
    Require(elapsed>=20000 && static_cast<unsigned long long>(elapsed)==native_clock_record.last.microseconds-start,35);
    start=native_clock_record.last.microseconds;
    elapsed=actual_v8_timer_elapsed(&timer);
    Require(elapsed>=0 && static_cast<unsigned long long>(elapsed)==native_clock_record.last.microseconds-start,36);
    actual_v8_timer_stop(&timer);Require(!actual_v8_timer_started(&timer),37);
    v8::base::TimeTicks last=v8::base::TimeTicks::FromInternalValue(static_cast<int64_t>(native_clock_record.last.microseconds)+1);
    elapsed=actual_v8_timer_elapsed_at(&whole,last);
    Require(elapsed>=20000 && static_cast<unsigned long long>(elapsed)==native_clock_record.last.microseconds-native_clock_record.first.microseconds,39);
    native_clock_record.elapsed_us=static_cast<unsigned long long>(elapsed);
    actual_v8_timer_stop(&whole);
    native_clock_record.legacy_last=static_cast<unsigned>(clock_probe_call(GTOS_SYS_TICKS,0,0));
    // Publish the diagnostic samples only after their complete stores. The
    // RequestExit mode performs no subsequent clock-record sample update.
    native_clock_record.stage=1;
#if GTOS_CLOCK_PROBE_MODE==1
    *reinterpret_cast<volatile unsigned*>(0xbfffcffcU)=0x434c4f43U;
#elif GTOS_CLOCK_PROBE_MODE==2
    for(;;)clock_probe_call(GTOS_SYS_YIELD,0,0);
#elif GTOS_CLOCK_PROBE_MODE==3
    for(;;){actual_v8_now();clock_probe_call(GTOS_SYS_YIELD,0,0);}
#else
    native_clock_record.stage=2;
    static const char pass[]="GTOS V8 CLOCK PASS TIME TICKS ELAPSED TIMER V1\n";
    Require(clock_probe_call(GTOS_SYS_WRITE,reinterpret_cast<unsigned>(pass),sizeof(pass)-1)==sizeof(pass)-1,38);
#endif
    return 0;
}
