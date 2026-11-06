#include "perfetto/ext/base/progress_reporter.h"
#include <stdlib.h>

namespace {
const char* force_color;
const char* no_color;
const char* term;
unsigned checks, failures;
bool Equal(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
void Check(bool value) { ++checks; if (!value) ++failures; }
}

// The environment boundary is injected; terminal/color decisions are made by
// the full, actual progress reporter source in each generated build profile.
extern "C" char* getenv(const char* name) noexcept {
  if (Equal(name, "FORCE_COLOR")) return const_cast<char*>(force_color);
  if (Equal(name, "NO_COLOR")) return const_cast<char*>(no_color);
  if (Equal(name, "TERM")) return const_cast<char*>(term);
  ++failures;
  return nullptr;
}

extern "C" int ConsoleTestMain() {
  using perfetto::base::StderrSupportsProgress;
  using perfetto::base::StderrSupportsColor;
  const char* terms[] = {nullptr, "", "xterm-256color", "dumb"};
  for (const char* value : terms) {
    term = value;
    force_color = no_color = nullptr;
    Check(!StderrSupportsProgress() && !StderrSupportsColor());
    force_color = "";
    Check(!StderrSupportsColor());
    no_color = "1";
    Check(!StderrSupportsProgress() && !StderrSupportsColor());
    force_color = "1";
    Check(StderrSupportsColor() && !StderrSupportsProgress());
    force_color = "0"; // Upstream treats any nonempty override as forced.
    Check(StderrSupportsColor());
    force_color = nullptr;
    no_color = "";
    Check(!StderrSupportsColor());
  }
  Check(checks == 24);
#if defined(GTOS_CONSOLE_WRONG_EXPECTATION)
  Check(StderrSupportsProgress());
#endif
  const char* output = failures ? "CONSOLE_CHECK_FAIL\n" : "CONSOLE_CHECK_PASS\n";
  unsigned length = 0;
  while (output[length]) ++length;
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(output), "d"(length) : "memory", "cc");
  return failures ? 1 : 0;
}
