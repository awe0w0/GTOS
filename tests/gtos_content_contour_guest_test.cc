// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <cstddef>

#include "third_party/blink/renderer/platform/geometry/contoured_rect.h"

int TestContentContour() {
    using blink::ContouredRect;
    using gfx::PointF;
    const gfx::RectF rect(-3, 5, 11, 19);
    const PointF top_left(-3, 5), top_right(8, 5);
    const PointF bottom_right(8, 24), bottom_left(-3, 24);
    const std::array<std::array<PointF, 4>, 5> expected{{
        {top_left, top_right, bottom_right, bottom_left},
        {top_right, bottom_right, bottom_left, top_left},
        {bottom_right, bottom_left, top_left, top_right},
        {bottom_left, top_left, top_right, bottom_right},
        {top_left, top_right, bottom_right, bottom_left},
    }};
    for (size_t rotation = 0; rotation < expected.size(); ++rotation) {
        const ContouredRect::Corner corner(rect, rotation, 0.5f);
        const std::array vertices{corner.Start(), corner.Outer(), corner.End(), corner.Center()};
        if (vertices != expected.at(rotation) || corner.Curvature() != 0.5f)
            return 128 + static_cast<int>(rotation);
    }

    const ContouredRect::Corner vertical(gfx::RectF(7, -4, 0, 10), 2, 1);
    return vertical.Start() == PointF(7, 6) && vertical.Outer() == PointF(7, 6) &&
        vertical.End() == PointF(7, -4) && vertical.Center() == PointF(7, -4) ? 0 : 133;
}
