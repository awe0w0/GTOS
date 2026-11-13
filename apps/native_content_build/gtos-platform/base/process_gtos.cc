// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <stdlib.h>

#include <utility>

#include "base/time/time.h"

namespace base {

Process::Process(ProcessHandle handle) : process_(handle) {}

Process::Process(Process&& other) : process_(other.Release()) {}

Process& Process::operator=(Process&& other) {
  process_ = other.Release();
  return *this;
}

Process::~Process() = default;

Process Process::Current() {
  return Process(GetCurrentProcessHandle());
}

Process Process::Open(ProcessId id) {
  return id == GetCurrentProcId() ? Current() : Process();
}

Process Process::OpenWithExtraPrivileges(ProcessId id) {
  return Open(id);
}

bool Process::CanSetPriority() {
  return false;
}

void Process::TerminateCurrentProcessImmediately(int exit_code) {
  _Exit(exit_code);
}

bool Process::IsValid() const {
  return is_current();
}

ProcessHandle Process::Handle() const {
  return process_;
}

Process Process::Duplicate() const {
  return Process(process_);
}

ProcessHandle Process::Release() {
  return std::exchange(process_, kNullProcessHandle);
}

ProcessId Process::Pid() const {
  return GetProcId(process_);
}

Time Process::CreationTime() const {
  // Process information exposes IDs and limits, but no creation timestamp.
  return Time();
}

bool Process::is_current() const {
  return process_ != kNullProcessHandle && process_ == GetCurrentProcessHandle();
}

void Process::Close() {
  process_ = kNullProcessHandle;
}

bool Process::Terminate(int exit_code, bool) const {
  if (is_current()) {
    TerminateCurrentProcessImmediately(exit_code);
  }
  return false;
}

bool Process::WaitForExit(int*) const {
  return false;
}

bool Process::WaitForExitWithTimeout(TimeDelta, int*) const {
  return false;
}

Process::Priority Process::GetPriority() const {
  return Priority::kUserBlocking;
}

bool Process::SetPriority(Priority) {
  return false;
}

int Process::GetOSPriority() const {
  return 0;
}

}  // namespace base
