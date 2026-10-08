#ifndef GTOS_V8_CLOCK_BRIDGE_H
#define GTOS_V8_CLOCK_BRIDGE_H
#include <stdint.h>
#include "src/base/platform/gtos-native-api/clock_abi.h"
extern "C" int gtos_v8_clock_validate(const GtosClockReadResult*);
extern "C" int64_t gtos_v8_clock_read_microseconds();
extern "C" [[noreturn]] void gtos_v8_clock_fatal();
#endif
