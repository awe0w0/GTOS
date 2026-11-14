// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/child_process_launcher_helper.h"

#include "content/browser/child_process_launcher.h"
#include "content/public/browser/child_process_termination_info.h"

namespace content::internal {

ChildProcessTerminationInfo ChildProcessLauncherHelper::GetTerminationInfo(
    const Process&, bool) {
  ChildProcessTerminationInfo info;
  info.status = base::TERMINATION_STATUS_LAUNCH_FAILED;
  info.exit_code = LAUNCH_RESULT_FAILURE;
  return info;
}

bool ChildProcessLauncherHelper::TerminateProcess(const base::Process&, int) {
  return false;
}

void ChildProcessLauncherHelper::ForceNormalProcessTerminationSync(Process) {}

void ChildProcessLauncherHelper::ApplyProcessPriorityOnLauncherThread(
    base::Process&, base::Process::Priority) {}

}  // namespace content::internal
