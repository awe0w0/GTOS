// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/shell_dialogs/select_file_dialog.h"

#include "ui/shell_dialogs/select_file_policy.h"

namespace ui {

SelectFileDialog* CreateSelectFileDialog(
    SelectFileDialog::Listener* listener,
    std::unique_ptr<SelectFilePolicy> policy) {
  // The shell treats an unavailable native picker as a canceled selection.
  return nullptr;
}

}  // namespace ui
