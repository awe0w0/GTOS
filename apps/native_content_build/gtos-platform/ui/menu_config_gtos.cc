// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_config.h"

namespace views {

void MenuConfig::InitPlatform() {
  // Views draws the menu and its border on the native Ozone surface.
  use_bubble_border = true;
}

}  // namespace views
