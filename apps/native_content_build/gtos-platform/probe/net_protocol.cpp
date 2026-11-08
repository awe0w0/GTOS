#include "memory_record.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "net/cookies/cookie_util.h"
#include "net/cookies/parsed_cookie.h"
#include "net/dns/dns_names_util.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

void Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
}

}  // namespace

extern "C" int NativeEntry() {
  using net::ParsedCookie;
  using net::cookie_util::ParseRequestCookieLine;
  using net::cookie_util::ParsedRequestCookies;
  using net::dns_names_util::DottedNameToNetwork;

  ParsedRequestCookies pairs;
  ParseRequestCookieLine("  a=b; c=\"d;e\"; empty=", &pairs);
  Check(pairs.size() == 3);
  Check(pairs.size() >= 1 && pairs[0].first == "a" && pairs[0].second == "b");
  Check(pairs.size() >= 2 && pairs[1].first == "c" &&
        pairs[1].second == "\"d;e\"");
  Check(pairs.size() >= 3 && pairs[2].first == "empty" &&
        pairs[2].second.empty());
  native_memory_record.base = pairs.size();

  pairs.clear();
  ParseRequestCookieLine("", &pairs);
  Check(pairs.empty());
  ParseRequestCookieLine("x=y; broken=\"unclosed", &pairs);
  Check(pairs.size() == 1 && pairs.front().first == "x" &&
        pairs.front().second == "y");
  Check(ParsedCookie::ParseTokenString(" \titem  =value") == "item");
  Check(ParsedCookie::ParseValueString(" \tvalue \t;next") == "value");
  Check(ParsedCookie::ParseTokenString("=x").empty());
  Check(ParsedCookie::ParseValueString(std::string_view("abc\0def", 7)) ==
        "abc");

  const auto wire = DottedNameToNetwork("www.gtos.");
  const std::array<uint8_t, 10> expected{
      3, 'w', 'w', 'w', 4, 'g', 't', 'o', 's', 0};
  Check(wire && wire->size() == expected.size());
#if defined(GTOS_NET_PROTOCOL_GUEST_WRONG_EXPECTATION)
  Check(wire && !wire->empty() && wire->front() == 4);
#else
  Check(wire && std::ranges::equal(*wire, expected));
#endif
  native_memory_record.fault_address = wire ? wire->size() : 0;
  Check(!DottedNameToNetwork("a..b"));
  Check(!DottedNameToNetwork(""));
  std::array<char, 64> label;
  label.fill('a');
  Check(DottedNameToNetwork(std::string_view(label.data(), 63)).has_value());
  Check(!DottedNameToNetwork(std::string_view(label.data(), label.size())));

  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}
