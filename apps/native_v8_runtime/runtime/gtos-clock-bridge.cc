#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-services.h"

#if !defined(__GTOS__) || !defined(__i386__)
#error This clock provider requires the native GTOS IA32 ABI
#endif

extern "C" int gtos_v8_clock_validate(const GtosClockReadResult* value) {
  return value && value->version == GTOS_CLOCK_ABI_VERSION &&
         value->clock_id == GTOS_CLOCK_ID_MONOTONIC &&
         value->unit == GTOS_CLOCK_UNIT_MICROSECONDS &&
         value->source == GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ &&
         value->capabilities == GTOS_CLOCK_REQUIRED_CAPABILITIES &&
         value->resolution_us == GTOS_CLOCK_RESOLUTION_US &&
         value->pit_input_hz == GTOS_CLOCK_PIT_INPUT_HZ &&
         value->pit_divisor == GTOS_CLOCK_PIT_DIVISOR &&
         value->microseconds <= 0x7ffffffffffffffdULL;
}

extern "C" [[noreturn]] void gtos_v8_clock_fatal() {
  v8::base::gtos::UnsupportedService();
}

extern "C" int64_t gtos_v8_clock_read_microseconds() {
  GtosClockReadResult value = {};
  GtosClockReadRequest request = {
      GTOS_CLOCK_ABI_VERSION, GTOS_CLOCK_ID_MONOTONIC, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&value)), sizeof(value)};
  unsigned call = GTOS_SYS_CLOCK_READ;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(&request))),
                 "c"(sizeof(request))
               : "memory", "cc");
  if (call != 0 || !gtos_v8_clock_validate(&value)) gtos_v8_clock_fatal();
  return static_cast<int64_t>(value.microseconds);
}
