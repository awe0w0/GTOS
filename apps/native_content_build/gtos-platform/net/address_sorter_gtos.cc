// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/address_sorter.h"

#include <algorithm>
#include <utility>

namespace net {
namespace {

class AddressSorterGtos final : public AddressSorter {
 public:
  void Sort(const std::vector<IPEndPoint>& endpoints,
            const NetworkAnonymizationKey& anonymization_key,
            handles::NetworkHandle target_network,
            CallbackType callback) const override {
    auto ordered = endpoints;
    // The GTOS network stack has IPv4 routes. Keep input order within each
    // address family and keep every endpoint's port.
    std::stable_partition(ordered.begin(), ordered.end(),
                          [](const IPEndPoint& endpoint) {
                            return endpoint.address().IsIPv4();
                          });
    std::move(callback).Run(true, std::move(ordered));
  }
};

}  // namespace

std::unique_ptr<AddressSorter> AddressSorter::CreateAddressSorter() {
  return std::make_unique<AddressSorterGtos>();
}

}  // namespace net
