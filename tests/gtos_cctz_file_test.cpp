#include "absl/time/internal/cctz/include/cctz/time_zone.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  namespace tz = absl::time_internal::cctz;
  const tz::time_point<tz::seconds> epoch{};
  tz::time_zone zone;
  assert(tz::load_time_zone("GTOS/Test", &zone));
  auto result = zone.lookup(epoch);
  assert(result.cs == tz::civil_second(1970, 1, 1, 5, 30, 0));
  assert(result.offset == 19800 && !result.is_dst);
  assert(std::strcmp(result.abbr, "GTS") == 0);
  assert(zone.lookup(result.cs).pre == epoch);

  assert(tz::load_time_zone("UTC", &zone));
  assert(zone.lookup(epoch).offset == 0);
  assert(tz::load_time_zone("Fixed/UTC-03:30:00", &zone));
  assert(zone.lookup(epoch).offset == -12600);
  for (const char* name : {"GTOS/Broken", "GTOS/Missing", "GTOS/../GTOS/Test",
                           "libc:UTC", "libc:localtime"}) {
    assert(!tz::load_time_zone(name, &zone));
    assert(zone.lookup(epoch).offset == 0);
  }
  std::puts("GTOS_CCTZ_FILE_PASS");
}
