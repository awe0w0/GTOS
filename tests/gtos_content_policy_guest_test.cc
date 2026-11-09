// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <iterator>
#include <string>

#include "common/FixedVector.h"
#include "services/network/public/cpp/content_security_policy/content_security_policy.cc"
#include "services/network/public/cpp/permissions_policy/permissions_policy_features_bitset.h"

int TestContentPolicy() {
    network::mojom::CSPSource source;
    if (!network::ParseScheme("A", &source) || source.scheme != "a" ||
        !network::ParseScheme("HtTp+1.-", &source) || source.scheme != "http+1.-")
        return 24;
    for (std::string_view bad : {std::string_view(), std::string_view("1http"),
                                 std::string_view("https:"),
                                 std::string_view("h\0tp", 4)}) {
        if (network::ParseScheme(bad, &source) || source.scheme != "http+1.-")
            return 25;
    }

    network::PermissionsPolicyFeaturesBitset features;
    const auto contains = [&features](unsigned index) {
        return features.Contains(static_cast<network::mojom::PermissionsPolicyFeature>(index));
    };
    const std::string original("\xaa\x01", 2);
    if (!features.Deserialize(original) || features.Serialize() != original ||
        !contains(0u) || contains(1u) || !contains(9u))
        return 26;
    const std::string too_long((features.bitset_size() + 7) / 8 + 1, 'x');
    if (features.Deserialize(too_long) || features.Serialize() != original) return 27;
    if (!features.Deserialize("\x02") || features.Serialize() != "\x02" ||
        contains(0u) || !contains(1u) || contains(9u))
        return 28;
    if (!features.Deserialize({}) || !features.Serialize().empty()) return 29;

    angle::FixedVector<int, 3> values;
    if (values.begin() != values.end()) return 30;
    values.push_back(2);
    values.push_back(3);
    values.push_back(5);
    const auto& fixed = values;
    if (std::distance(fixed.begin(), fixed.end()) != 3 ||
        !std::ranges::equal(fixed, std::array<int, 3>{2, 3, 5})) return 31;
    values.resize(1);
    if (std::distance(values.begin(), values.end()) != 1 || *values.begin() != 2)
        return 32;
    return 0;
}
