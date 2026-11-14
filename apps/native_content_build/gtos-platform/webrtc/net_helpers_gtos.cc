// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtc_base/net_helpers.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "url/url_canon_ip.h"

namespace webrtc {
namespace {

constexpr std::array<uint8_t, 12> kMappedPrefix = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
};

// URL host parsing accepts abbreviations and nondecimal components. The
// WebRTC input is four decimal octets with neither extra characters nor
// leading zeroes.
bool ParseV4(std::string_view input, std::array<uint8_t, 4>& address) {
  for (size_t i = 0; i < address.size(); ++i) {
    const size_t dot = input.find('.');
    if ((dot == input.npos) != (i == address.size() - 1)) {
      return false;
    }
    const auto octet = input.substr(0, dot);
    if (octet.empty() || octet.size() > 3 ||
        (octet.size() > 1 && octet.front() == '0')) {
      return false;
    }
    unsigned value = 0;
    for (char ch : octet) {
      if (ch < '0' || ch > '9') {
        return false;
      }
      value = value * 10 + (ch - '0');
    }
    if (value > 255) {
      return false;
    }
    address[i] = static_cast<uint8_t>(value);
    if (dot != input.npos) {
      input.remove_prefix(dot + 1);
    }
  }
  return true;
}

bool IsMapped(const std::array<uint8_t, 16>& address) {
  return std::equal(kMappedPrefix.begin(), kMappedPrefix.end(), address.begin());
}

bool ParseV6(std::string_view input, std::array<uint8_t, 16>& address) {
  // The longest mixed address is six groups plus dotted IPv4.
  constexpr size_t kMaxAddressLength = 45;
  if (input.empty() || input.size() > kMaxAddressLength) {
    return false;
  }
  const bool mixed = input.find('.') != input.npos;
  if (mixed) {
    const size_t colon = input.rfind(':');
    std::array<uint8_t, 4> v4{};
    if (colon == input.npos || !ParseV4(input.substr(colon + 1), v4)) {
      return false;
    }
  }
  std::array<char, kMaxAddressLength + 2> bracketed;
  bracketed.front() = '[';
  std::copy(input.begin(), input.end(), bracketed.begin() + 1);
  bracketed[input.size() + 1] = ']';
  // WebRTC's mixed notation is reserved for mapped IPv4 addresses.
  return url::IPv6AddressToNumber(
             std::string_view(bracketed.data(), input.size() + 2), address) &&
         (!mixed || IsMapped(address));
}

}  // namespace

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
  url::RawCanonOutput<46> output;
  if (af == AF_INET) {
    std::array<uint8_t, 4> address;
    std::memcpy(address.data(), src, address.size());
    url::AppendIPv4Address(address, &output);
  } else if (af == AF_INET6) {
    std::array<uint8_t, 16> address;
    std::memcpy(address.data(), src, address.size());
    if (IsMapped(address)) {
      output.Append("::ffff:");
      url::AppendIPv4Address(base::span(address).last<4>(), &output);
    } else {
      url::AppendIPv6Address(address, &output);
    }
  } else {
    errno = EINVAL;
    return nullptr;
  }
  output.push_back('\0');
  const auto text = output.view();
  if (text.size() > size) {
    errno = ENOSPC;
    return nullptr;
  }
  std::memcpy(dst, text.data(), text.size());
  return dst;
}

int inet_pton(int af, absl::string_view src, void* dst) {
  if (af == AF_INET) {
    std::array<uint8_t, 4> address{};
    if (!ParseV4(src, address)) {
      return 0;
    }
    std::memcpy(dst, address.data(), address.size());
    return 1;
  }
  if (af == AF_INET6) {
    std::array<uint8_t, 16> address{};
    if (!ParseV6(src, address)) {
      return 0;
    }
    std::memcpy(dst, address.data(), address.size());
    return 1;
  }
  errno = EINVAL;
  return -1;
}

}  // namespace webrtc
