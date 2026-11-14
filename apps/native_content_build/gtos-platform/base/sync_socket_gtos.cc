// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sync_socket.h"

namespace base {

bool SyncSocket::CreatePair(SyncSocket*, SyncSocket*) {
  // GTOS does not yet expose socket capabilities through PlatformFile.
  return false;
}

void SyncSocket::Close() {
  handle_.reset();
}

size_t SyncSocket::Send(span<const uint8_t>) {
  return 0;
}

size_t SyncSocket::Receive(span<uint8_t>) {
  return 0;
}

size_t SyncSocket::ReceiveWithTimeout(span<uint8_t>, TimeDelta) {
  return 0;
}

size_t SyncSocket::Peek() {
  return 0;
}

bool SyncSocket::IsValid() const {
  return handle_.is_valid();
}

SyncSocket::Handle SyncSocket::handle() const {
  return handle_.get();
}

SyncSocket::Handle SyncSocket::Release() {
  return handle_.release();
}

bool CancelableSyncSocket::CreatePair(CancelableSyncSocket* left,
                                      CancelableSyncSocket* right) {
  return SyncSocket::CreatePair(left, right);
}

bool CancelableSyncSocket::Shutdown() {
  return false;
}

size_t CancelableSyncSocket::Send(span<const uint8_t> data) {
  return SyncSocket::Send(data);
}

}  // namespace base
