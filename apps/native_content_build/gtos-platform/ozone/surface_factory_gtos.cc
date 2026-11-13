// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/gtos/surface_factory_gtos.h"

#include <process/native_window_frame.h>

#include <limits>
#include <memory>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace ui {
namespace {

class GtosCanvas : public SurfaceOzoneCanvas {
 public:
  explicit GtosCanvas(unsigned handle) : handle_(handle) {}

  SkCanvas* GetCanvas() override {
    return surface_ ? surface_->getCanvas() : nullptr;
  }

  void ResizeCanvas(const gfx::Size& viewport, float scale) override {
    surface_.reset();
    if (viewport.IsEmpty()) {
      return;
    }
    CHECK_LE(viewport.width(), static_cast<int>(GTOS_WINDOW_LARGE_WIDTH));
    CHECK_LE(viewport.height(), static_cast<int>(GTOS_WINDOW_LARGE_HEIGHT));
    auto info = SkImageInfo::Make(viewport.width(), viewport.height(),
                                  kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    pixels_.assign(info.computeMinByteSize(), 0);
    surface_ = SkSurfaces::WrapPixels(info, pixels_.data(), info.minRowBytes());
    CHECK(surface_);
  }

  void PresentCanvas(const gfx::Rect& damage) override {
    if (!surface_) {
      return;
    }
    const int result = gtos::process::PresentLargeNativeWindowFrame(
        handle_, pixels_.data(), static_cast<unsigned>(pixels_.size()));
    if (result == GTOS_WINDOW_ERR_BAD_HANDLE) {
      return;
    }
    if (result == GTOS_WINDOW_ERR_BAD_STATE) {
      GtosWindowInfoResponse info;
      const GtosWindowInfoRequest query{
          GTOS_WINDOW_ABI_VERSION, handle_, reinterpret_cast<unsigned>(&info)};
      const int status = gtos::process::WindowInfo(&query);
      if (status == GTOS_WINDOW_ERR_BAD_HANDLE ||
          (status == 0 && info.lifecycle == GTOS_WINDOW_CLOSING)) {
        return;
      }
    }
    CHECK_EQ(result, 0);
  }

  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override {
    return nullptr;
  }

 private:
  const unsigned handle_;
  std::vector<unsigned char> pixels_;
  sk_sp<SkSurface> surface_;
};

}  // namespace

std::unique_ptr<SurfaceOzoneCanvas> SurfaceFactoryGtos::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  CHECK_GT(widget, 0u);
  CHECK_LE(widget, static_cast<gfx::AcceleratedWidget>(
                       std::numeric_limits<int>::max()));
  return std::make_unique<GtosCanvas>(static_cast<unsigned>(widget));
}

}  // namespace ui
