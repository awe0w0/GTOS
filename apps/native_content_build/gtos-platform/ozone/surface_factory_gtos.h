// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_GTOS_SURFACE_FACTORY_GTOS_H_
#define UI_OZONE_PLATFORM_GTOS_SURFACE_FACTORY_GTOS_H_

#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class SurfaceFactoryGtos : public SurfaceFactoryOzone {
 public:
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_GTOS_SURFACE_FACTORY_GTOS_H_
