#ifndef GTOS_CLIPBOARD_PROBE_API_H
#define GTOS_CLIPBOARD_PROBE_API_H

#include <stdint.h>

struct NativeClipboardRecord {
  uint32_t version, mode, stage, error, checks, mask, result, constructors;
  uint32_t format_cases, serialization_cases, comparison_cases, lifetime_waves;
  uint32_t custom_cases, web_custom_cases, retained_bytes, ascii_rejection_attempts;
  uint32_t reserved[48];
};
static_assert(sizeof(uint32_t) == 4 && sizeof(int) == 4 &&
              sizeof(NativeClipboardRecord) == 256);

extern "C" {
extern volatile NativeClipboardRecord native_clipboard_record;
uint32_t NativeClipboardChecks(volatile NativeClipboardRecord* record);
[[noreturn]] void NativeClipboardEntry();
}

#endif
