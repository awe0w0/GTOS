#include "probe.h"
extern "C" int gtos_v8_clock_validate(const GtosClockReadResult* value) {
    return value && value->version==GTOS_CLOCK_ABI_VERSION &&
        value->clock_id==GTOS_CLOCK_ID_MONOTONIC &&
        value->unit==GTOS_CLOCK_UNIT_MICROSECONDS &&
        value->source==GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ &&
        value->capabilities==GTOS_CLOCK_REQUIRED_CAPABILITIES &&
        value->resolution_us==GTOS_CLOCK_RESOLUTION_US &&
        value->pit_input_hz==GTOS_CLOCK_PIT_INPUT_HZ &&
        value->pit_divisor==GTOS_CLOCK_PIT_DIVISOR &&
        value->microseconds<=0x7ffffffffffffffdULL;
}
static void StoreResult(volatile GtosClockReadResult* destination,const GtosClockReadResult* source) {
    volatile unsigned char* output=reinterpret_cast<volatile unsigned char*>(destination);
    const unsigned char* input=reinterpret_cast<const unsigned char*>(source);
    for(unsigned i=0;i<sizeof(*source);++i)output[i]=input[i];
}
extern "C" int64_t gtos_v8_clock_read_microseconds() {
    GtosClockReadResult value;
    unsigned char* bytes=reinterpret_cast<unsigned char*>(&value);
    for(unsigned i=0;i<sizeof(value);++i)bytes[i]=0xa5U;
    GtosClockReadRequest request={GTOS_CLOCK_ABI_VERSION,GTOS_CLOCK_ID_MONOTONIC,0,
        reinterpret_cast<unsigned>(&value),sizeof(value)};
    int result=clock_probe_call(GTOS_SYS_CLOCK_READ,reinterpret_cast<unsigned>(&request),sizeof(request));
    native_clock_record.last_result=static_cast<unsigned>(result);
    if(result!=0){native_clock_record.clock_failures=native_clock_record.clock_failures+1;gtos_v8_clock_fatal();}
    StoreResult(&native_clock_record.last,&value);
    if(native_clock_record.first.version==0){
        StoreResult(&native_clock_record.first,&value);
        native_clock_record.first_result=0;
    }
    if(!gtos_v8_clock_validate(&value)){
        native_clock_record.clock_failures=native_clock_record.clock_failures+1;
        gtos_v8_clock_fatal();
    }
    // Conversion is checked before the original TimeTicks::Now adds one.
    return static_cast<int64_t>(value.microseconds);
}
