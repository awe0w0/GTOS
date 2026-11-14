// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_config_service.h"

namespace net {

std::unique_ptr<DnsConfigService> DnsConfigService::CreateSystemService() {
  // GTOS has no system resolver settings to watch.
  return nullptr;
}

}  // namespace net
