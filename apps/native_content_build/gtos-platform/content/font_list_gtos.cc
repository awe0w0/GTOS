// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/font_list.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkString.h"

namespace content {

base::ListValue GetFontList_SlowBlocking() {
  DCHECK(GetFontListTaskRunner()->RunsTasksInCurrentSequence());
  auto manager = skia::DefaultFontMgr();
  base::ListValue families;
  for (int i = 0; i < manager->countFamilies(); ++i) {
    SkString name;
    manager->getFamilyName(i, &name);
    if (name.isEmpty()) {
      continue;
    }
    const std::string family(name.c_str(), name.size());
    base::ListValue entry;
    entry.Append(family);
    entry.Append(family);
    families.Append(std::move(entry));
  }
  return families;
}

}  // namespace content
