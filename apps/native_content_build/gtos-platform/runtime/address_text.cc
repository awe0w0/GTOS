// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>

#include <algorithm>
#include <array>
#include <string_view>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "url/url_canon_ip.h"

namespace {

bool ParseV4(std::string_view text, std::array<uint8_t, 4>& address) {
  for (size_t i = 0; i < address.size(); ++i) {
    const size_t dot = text.find('.');
    if ((dot == text.npos) != (i + 1 == address.size())) {
      return false;
    }
    const auto octet = text.substr(0, dot);
    if (octet.empty() || octet.size() > 3 ||
        (octet.size() > 1 && octet.front() == '0')) {
      return false;
    }
    unsigned value = 0;
    for (char digit : octet) {
      if (digit < '0' || digit > '9') {
        return false;
      }
      value = value * 10 + digit - '0';
    }
    if (value > 255) {
      return false;
    }
    address[i] = static_cast<uint8_t>(value);
    if (dot != text.npos) {
      text.remove_prefix(dot + 1);
    }
  }
  return true;
}

bool ParseV6(std::string_view text, std::array<uint8_t, 16>& address) {
  if (text.empty() || text.size() >= INET6_ADDRSTRLEN) {
    return false;
  }
  if (text.find('.') != text.npos) {
    const auto colon = text.rfind(':');
    std::array<uint8_t, 4> tail{};
    if (colon == text.npos || !ParseV4(text.substr(colon + 1), tail)) {
      return false;
    }
  }
  std::array<char, INET6_ADDRSTRLEN + 1> bracketed;
  bracketed.front() = '[';
  base::span(bracketed).subspan<1>().first(text.size()).copy_from(
      base::span(text));
  bracketed[text.size() + 1] = ']';
  return url::IPv6AddressToNumber(
      std::string_view(bracketed.data(), text.size() + 2), address);
}

}  // namespace

extern "C" int inet_pton(int family, const char* text, void* address) {
  if (family != AF_INET && family != AF_INET6) {
    errno = EINVAL;
    return -1;
  }
  if (!text || !address) {
    errno = EINVAL;
    return -1;
  }
  if (family == AF_INET) {
    std::array<uint8_t, 4> bytes;
    if (!ParseV4(text, bytes)) {
      return 0;
    }
    // SAFETY: inet_pton requires a writable IPv4 address for AF_INET.
    UNSAFE_BUFFERS(base::span(static_cast<uint8_t*>(address), bytes.size()))
        .copy_from(bytes);
  } else {
    std::array<uint8_t, 16> bytes;
    if (!ParseV6(text, bytes)) {
      return 0;
    }
    // SAFETY: inet_pton requires a writable IPv6 address for AF_INET6.
    UNSAFE_BUFFERS(base::span(static_cast<uint8_t*>(address), bytes.size()))
        .copy_from(bytes);
  }
  return 1;
}

extern "C" const char* inet_ntop(int family, const void* address, char* text,
                                 socklen_t size) {
  if (family != AF_INET && family != AF_INET6) {
    errno = EINVAL;
    return nullptr;
  }
  if (!address || !text) {
    errno = EINVAL;
    return nullptr;
  }
  url::RawCanonOutput<INET6_ADDRSTRLEN> output;
  if (family == AF_INET) {
    std::array<uint8_t, 4> bytes;
    // SAFETY: inet_ntop requires a readable IPv4 address for AF_INET.
    base::span(bytes).copy_from(UNSAFE_BUFFERS(
        base::span(static_cast<const uint8_t*>(address), bytes.size())));
    url::AppendIPv4Address(bytes, &output);
  } else {
    constexpr std::array<uint8_t, 12> mapped{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};
    std::array<uint8_t, 16> bytes;
    // SAFETY: inet_ntop requires a readable IPv6 address for AF_INET6.
    base::span(bytes).copy_from(UNSAFE_BUFFERS(
        base::span(static_cast<const uint8_t*>(address), bytes.size())));
    if (std::equal(mapped.begin(), mapped.end(), bytes.begin())) {
      output.Append("::ffff:");
      url::AppendIPv4Address(base::span(bytes).last<4>(), &output);
    } else {
      url::AppendIPv6Address(bytes, &output);
    }
  }
  output.push_back('\0');
  const auto result = output.view();
  if (result.size() > size) {
    errno = ENOSPC;
    return nullptr;
  }
  // SAFETY: The caller supplies size writable bytes, and the output fits.
  UNSAFE_BUFFERS(base::span(text, size))
      .first(result.size())
      .copy_from(base::span(result));
  return text;
}
