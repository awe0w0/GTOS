#include "memory_record.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <array>

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
  Check(htons(0x1234) == 0x3412 && ntohs(0x3412) == 0x1234);
  Check(htonl(0x12345678) == 0x78563412 && ntohl(0x78563412) == 0x12345678);

  std::array<uint8_t, 4> ipv4{};
  const std::array<uint8_t, 4> loopback{127, 0, 0, 1};
  Check(inet_pton(AF_INET, "127.0.0.1", ipv4.data()) == 1);
  Check(ipv4 == loopback);
  std::array<char, INET_ADDRSTRLEN> text{};
  text.fill('!');
  Check(inet_ntop(AF_INET, ipv4.data(), text.data(), 10) == text.data());
#if defined(GTOS_ADDRESS_TEXT_GUEST_WRONG_EXPECTATION)
  Check(strcmp(text.data(), "127.0.0.2") == 0 && text[10] == '!');
#else
  Check(strcmp(text.data(), "127.0.0.1") == 0 && text[10] == '!');
#endif
  const auto formatted = text;
  errno = 0;
  Check(inet_ntop(AF_INET, ipv4.data(), text.data(), 9) == nullptr);
  Check(errno == ENOSPC && text == formatted);
  Check(inet_pton(AF_INET, "256.0.0.1", ipv4.data()) == 0);
  Check(inet_pton(AF_INET, "127.0.00.1", ipv4.data()) == 0);
  Check(inet_pton(AF_INET, "127.0.1", ipv4.data()) == 0);
  Check(ipv4 == loopback);
  native_memory_record.base = (unsigned(ipv4[0]) << 24) | ipv4[3];

  std::array<uint8_t, 16> ipv6{};
  const std::array<uint8_t, 16> documentation{
      0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  const std::array<uint8_t, 16> mapped{
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 192, 0, 2, 1};
  std::array<char, INET6_ADDRSTRLEN> long_text{};
  Check(inet_pton(AF_INET6, "2001:db8::1", ipv6.data()) == 1);
  Check(ipv6 == documentation);
  Check(inet_ntop(AF_INET6, ipv6.data(), long_text.data(), long_text.size()) ==
            long_text.data() &&
        strcmp(long_text.data(), "2001:db8::1") == 0);
  Check(inet_pton(AF_INET6, "::ffff:192.0.2.1", ipv6.data()) == 1);
  Check(ipv6 == mapped);
  Check(inet_ntop(AF_INET6, ipv6.data(), long_text.data(), long_text.size()) ==
            long_text.data() &&
        strcmp(long_text.data(), "::ffff:192.0.2.1") == 0);
  Check(inet_pton(AF_INET6, "fe80::1%1", ipv6.data()) == 0);
  Check(inet_pton(AF_INET6, "::ffff:192.0.02.1", ipv6.data()) == 0);
  Check(ipv6 == mapped);
  native_memory_record.fault_address =
      (unsigned(ipv6[12]) << 24) | (unsigned(ipv6[14]) << 8) | ipv6[15];

  errno = 0;
  Check(inet_pton(AF_UNSPEC, "::1", ipv6.data()) == -1 && errno == EINVAL);
  errno = 0;
  Check(inet_ntop(AF_UNSPEC, ipv6.data(), long_text.data(), long_text.size()) ==
            nullptr &&
        errno == EINVAL);
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}
