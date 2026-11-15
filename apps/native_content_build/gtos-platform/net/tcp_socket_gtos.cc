// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/tcp_socket_gtos.h"

#include <algorithm>
#include <utility>

#include <process/loopback_abi.h>
#include <process/tcp_abi.h>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/time/time.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"

namespace net {
namespace {

template <class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "cc", "memory");
  return static_cast<int>(operation);
}

int Control(unsigned operation, unsigned handle) {
  return Call(operation, GtosLoopbackControlRequest{
                             GTOS_LOOPBACK_ABI_VERSION, handle});
}

int Option(unsigned operation, unsigned handle, unsigned value) {
  return Call(operation, GtosLoopbackOptionRequest{
                             GTOS_LOOPBACK_ABI_VERSION, handle, value});
}

int MapResult(int result) {
  switch (result) {
    case GTOS_LOOPBACK_ERR_BAD_HANDLE:
      return ERR_INVALID_HANDLE;
    case GTOS_LOOPBACK_ERR_AGAIN:
      return ERR_IO_PENDING;
    case GTOS_LOOPBACK_ERR_BAD_ADDRESS:
    case GTOS_LOOPBACK_ERR_INVALID:
      return ERR_INVALID_ARGUMENT;
    case GTOS_LOOPBACK_ERR_LIMIT:
      return ERR_INSUFFICIENT_RESOURCES;
    case GTOS_LOOPBACK_ERR_BROKEN_PIPE:
      return ERR_CONNECTION_CLOSED;
    case GTOS_LOOPBACK_ERR_UNAVAILABLE:
      return ERR_NOT_IMPLEMENTED;
    case GTOS_LOOPBACK_ERR_ADDRESS_IN_USE:
      return ERR_ADDRESS_IN_USE;
    case GTOS_LOOPBACK_ERR_NOT_CONNECTED:
      return ERR_SOCKET_NOT_CONNECTED;
    case GTOS_LOOPBACK_ERR_REFUSED:
      return ERR_CONNECTION_REFUSED;
    default:
      return result >= 0 ? result : ERR_FAILED;
  }
}

int MapTcpResult(int result) {
  switch (result) {
    case GTOS_TCP_ERR_DENIED:
      return ERR_ACCESS_DENIED;
    case GTOS_TCP_ERR_UNAVAILABLE:
      return ERR_ADDRESS_UNREACHABLE;
    case GTOS_TCP_ERR_RESET:
      return ERR_CONNECTION_RESET;
    case GTOS_TCP_ERR_TIMEOUT:
      return ERR_CONNECTION_TIMED_OUT;
    default:
      // The stream ABI shares the other error values with loopback.
      return MapResult(result);
  }
}

IPAddress FromWireAddress(unsigned address) {
  return IPAddress(address & 0xff, (address >> 8) & 0xff,
                   (address >> 16) & 0xff, address >> 24);
}

}  // namespace

struct TCPSocketGtos::SocketInfo {
  IPEndPoint local;
  IPEndPoint peer;
  bool readable;
  bool eof;
};

std::unique_ptr<TCPSocketGtos> TCPSocketGtos::Create(
    std::unique_ptr<SocketPerformanceWatcher> watcher,
    net::NetLog* net_log,
    const NetLogSource&) {
  return Create(std::move(watcher),
                NetLogWithSource::Make(net_log, NetLogSourceType::SOCKET));
}

std::unique_ptr<TCPSocketGtos> TCPSocketGtos::Create(
    std::unique_ptr<SocketPerformanceWatcher> watcher,
    NetLogWithSource net_log) {
  return std::unique_ptr<TCPSocketGtos>(
      new TCPSocketGtos(std::move(watcher), net_log));
}

TCPSocketGtos::TCPSocketGtos(
    std::unique_ptr<SocketPerformanceWatcher> watcher,
    NetLogWithSource net_log)
    : watcher_(std::move(watcher)), net_log_(net_log) {}

TCPSocketGtos::~TCPSocketGtos() {
  Close();
}

int TCPSocketGtos::Open(AddressFamily family) {
  if (IsValid()) {
    return ERR_INVALID_ARGUMENT;
  }
  if (family != ADDRESS_FAMILY_IPV4) {
    return ERR_ADDRESS_UNREACHABLE;
  }
  const int result = Control(GTOS_SYS_LOOPBACK_OPEN, 0);
  if (result <= 0) {
    return result ? MapResult(result) : ERR_UNEXPECTED;
  }
  handle_ = result;
  return OK;
}

int TCPSocketGtos::Bind(const IPEndPoint& address) {
  if (!IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (physical_ || address.address() != IPAddress::IPv4Localhost()) {
    return ERR_ADDRESS_UNREACHABLE;
  }
  const int result =
      MapResult(Option(GTOS_SYS_LOOPBACK_BIND, handle_, address.port()));
  if (result == OK) {
    loopback_in_use_ = true;
  }
  return result;
}

int TCPSocketGtos::Listen(int backlog) {
  if (!IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (physical_ || backlog <= 0) {
    return ERR_INVALID_ARGUMENT;
  }
  return MapResult(Option(GTOS_SYS_LOOPBACK_LISTEN, handle_, backlog));
}

int TCPSocketGtos::Connect(const IPEndPoint& address,
                           CompletionOnceCallback callback) {
  if (!IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (physical_) {
    return ERR_INVALID_ARGUMENT;
  }
  if (address.address() == IPAddress::IPv4Localhost()) {
    const int result =
        MapResult(Option(GTOS_SYS_LOOPBACK_CONNECT, handle_, address.port()));
    if (result == OK) {
      loopback_in_use_ = true;
    }
    return result;
  }
  if (loopback_in_use_ || !address.address().IsIPv4()) {
    return ERR_ADDRESS_UNREACHABLE;
  }
  const auto& bytes = address.address().bytes();
  const unsigned peer = unsigned{bytes[0]} | (unsigned{bytes[1]} << 8) |
                        (unsigned{bytes[2]} << 16) | (unsigned{bytes[3]} << 24);
  const int handle =
      Call(GTOS_SYS_TCP_CONNECT,
           GtosTcpConnectRequest{GTOS_TCP_ABI_VERSION, peer, address.port(), 0});
  if (handle <= 0) {
    return handle ? MapTcpResult(handle) : ERR_UNEXPECTED;
  }
  Control(GTOS_SYS_LOOPBACK_CLOSE, handle_);
  handle_ = handle;
  physical_ = true;
  SocketInfo info;
  const int result = GetSocketInfo(info);
  if (result == ERR_IO_PENDING) {
    CHECK(callback);
    connect_callback_ = std::move(callback);
    Arm(connect_timer_, &TCPSocketGtos::CompleteConnect);
  }
  return result;
}

int TCPSocketGtos::TryAccept(std::unique_ptr<TCPSocketGtos>* socket,
                             IPEndPoint* address) {
  const int result = Control(GTOS_SYS_LOOPBACK_ACCEPT, handle_);
  if (result <= 0) {
    return result ? MapResult(result) : ERR_UNEXPECTED;
  }
  auto accepted = Create(nullptr, net_log_);
  accepted->handle_ = result;
  accepted->loopback_in_use_ = true;
  if (address) {
    const int error = accepted->GetPeerAddress(address);
    if (error != OK) {
      return error;
    }
  }
  *socket = std::move(accepted);
  return OK;
}

int TCPSocketGtos::Accept(std::unique_ptr<TCPSocketGtos>* socket,
                           IPEndPoint* address,
                           CompletionOnceCallback callback) {
  CHECK(!accept_.callback);
  CHECK(socket);
  if (!IsValid() || physical_) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  const int result = TryAccept(socket, address);
  if (result == ERR_IO_PENDING) {
    CHECK(callback);
    accept_ = {socket, address, std::move(callback)};
    Arm(accept_timer_, &TCPSocketGtos::CompleteAccept);
  }
  return result;
}

int TCPSocketGtos::TryTransfer(bool reading, IOBuffer* buffer, int length) {
  if (!IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (!buffer || length <= 0) {
    return ERR_INVALID_ARGUMENT;
  }
  if (physical_) {
    return MapTcpResult(Call(reading ? GTOS_SYS_TCP_READ : GTOS_SYS_TCP_WRITE,
        GtosTcpTransferRequest{GTOS_TCP_ABI_VERSION,
          static_cast<unsigned>(handle_), reinterpret_cast<unsigned>(buffer->data()),
          std::min(static_cast<unsigned>(length), GTOS_TCP_TRANSFER_LIMIT)}));
  }
  return MapResult(Call(reading ? GTOS_SYS_LOOPBACK_READ : GTOS_SYS_LOOPBACK_WRITE,
      GtosLoopbackTransferRequest{GTOS_LOOPBACK_ABI_VERSION,
        static_cast<unsigned>(handle_), reinterpret_cast<unsigned>(buffer->data()),
        std::min(static_cast<unsigned>(length), GTOS_LOOPBACK_TRANSFER_LIMIT)}));
}

int TCPSocketGtos::ReadInternal(IOBuffer* buffer, int length,
                                CompletionOnceCallback callback,
                                bool readiness_only) {
  CHECK(!read_.callback);
  const int result = TryTransfer(true, buffer, length);
  if (result == ERR_IO_PENDING) {
    CHECK(callback);
    if (!readiness_only) {
      read_.buffer = base::WrapRefCounted(buffer);
    }
    read_.length = length;
    read_.callback = std::move(callback);
    readiness_only_ = readiness_only;
    Arm(read_timer_, &TCPSocketGtos::CompleteRead);
  }
  return result;
}

int TCPSocketGtos::Read(IOBuffer* buffer, int length,
                        CompletionOnceCallback callback) {
  return ReadInternal(buffer, length, std::move(callback), false);
}

int TCPSocketGtos::ReadIfReady(IOBuffer* buffer, int length,
                               CompletionOnceCallback callback) {
  return ReadInternal(buffer, length, std::move(callback), true);
}

int TCPSocketGtos::CancelReadIfReady() {
  if (!read_.callback || !readiness_only_) {
    return ERR_UNEXPECTED;
  }
  read_timer_.Stop();
  read_ = {};
  readiness_only_ = false;
  return OK;
}

int TCPSocketGtos::Write(IOBuffer* buffer, int length,
                         CompletionOnceCallback callback,
                         const NetworkTrafficAnnotationTag&) {
  CHECK(!write_.callback);
  const int result = TryTransfer(false, buffer, length);
  if (result == ERR_IO_PENDING) {
    CHECK(callback);
    write_ = {base::WrapRefCounted(buffer), length, std::move(callback)};
    Arm(write_timer_, &TCPSocketGtos::CompleteWrite);
  }
  return result;
}

void TCPSocketGtos::Arm(base::OneShotTimer& timer,
                       void (TCPSocketGtos::*method)()) {
  // The GTOS IO pump has a task timer for polling stream readiness.
  timer.Start(FROM_HERE, base::Milliseconds(10), this, method);
}

void TCPSocketGtos::CompleteAccept() {
  const int result = TryAccept(accept_.result, accept_.address);
  if (result == ERR_IO_PENDING) {
    Arm(accept_timer_, &TCPSocketGtos::CompleteAccept);
    return;
  }
  auto callback = std::move(accept_.callback);
  accept_ = {};
  std::move(callback).Run(result);
}

void TCPSocketGtos::CompleteConnect() {
  SocketInfo info;
  const int result = GetSocketInfo(info);
  if (result == ERR_IO_PENDING) {
    Arm(connect_timer_, &TCPSocketGtos::CompleteConnect);
    return;
  }
  auto callback = std::move(connect_callback_);
  std::move(callback).Run(result);
}

void TCPSocketGtos::CompleteRead() {
  int result;
  if (readiness_only_) {
    SocketInfo info;
    result = GetSocketInfo(info);
    if (result == OK && !info.readable && !info.eof) {
      result = ERR_IO_PENDING;
    }
  } else {
    result = TryTransfer(true, read_.buffer.get(), read_.length);
  }
  if (result == ERR_IO_PENDING) {
    Arm(read_timer_, &TCPSocketGtos::CompleteRead);
    return;
  }
  auto callback = std::move(read_.callback);
  read_ = {};
  readiness_only_ = false;
  std::move(callback).Run(result);
}

void TCPSocketGtos::CompleteWrite() {
  const int result = TryTransfer(false, write_.buffer.get(), write_.length);
  if (result == ERR_IO_PENDING) {
    Arm(write_timer_, &TCPSocketGtos::CompleteWrite);
    return;
  }
  auto callback = std::move(write_.callback);
  write_ = {};
  std::move(callback).Run(result);
}

int TCPSocketGtos::GetSocketInfo(SocketInfo& info) const {
  if (!IsValid()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  if (physical_) {
    GtosTcpInfo wire{};
    const int result = MapTcpResult(Call(
        GTOS_SYS_TCP_INFO, GtosTcpInfoRequest{GTOS_TCP_ABI_VERSION,
            static_cast<unsigned>(handle_), reinterpret_cast<unsigned>(&wire)}));
    if (result != OK) {
      return result;
    }
    if (wire.version != GTOS_TCP_ABI_VERSION) {
      return ERR_UNEXPECTED;
    }
    if (wire.status != OK) {
      return MapTcpResult(wire.status);
    }
    info = {IPEndPoint(FromWireAddress(wire.local_ipv4), wire.local_port),
            IPEndPoint(FromWireAddress(wire.peer_ipv4), wire.peer_port),
            wire.readable != 0, (wire.flags & GTOS_TCP_END_OF_STREAM) != 0};
    return OK;
  }
  GtosLoopbackInfo wire{};
  const int result = MapResult(Option(GTOS_SYS_LOOPBACK_INFO, handle_,
                                     reinterpret_cast<unsigned>(&wire)));
  if (result != OK) {
    return result;
  }
  info = {IPEndPoint(IPAddress::IPv4Localhost(), wire.local_port),
          IPEndPoint(IPAddress::IPv4Localhost(), wire.peer_port),
          (wire.ready & GTOS_LOOPBACK_READABLE) != 0,
          (wire.ready & GTOS_LOOPBACK_END_OF_STREAM) != 0};
  return OK;
}

int TCPSocketGtos::GetLocalAddress(IPEndPoint* address) const {
  SocketInfo info;
  const int result = GetSocketInfo(info);
  if (result != OK) {
    return result;
  }
  if (!info.local.port()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  *address = info.local;
  return OK;
}

int TCPSocketGtos::GetPeerAddress(IPEndPoint* address) const {
  SocketInfo info;
  const int result = GetSocketInfo(info);
  if (result != OK) {
    return result;
  }
  if (!info.peer.port()) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  *address = info.peer;
  return OK;
}

bool TCPSocketGtos::IsConnected() const {
  SocketInfo info;
  return GetSocketInfo(info) == OK && info.peer.port() &&
         (!info.eof || info.readable);
}

bool TCPSocketGtos::IsConnectedAndIdle() const {
  SocketInfo info;
  return GetSocketInfo(info) == OK && info.peer.port() &&
         !info.readable && !info.eof;
}

int TCPSocketGtos::SetDefaultOptionsForServer() {
  return IsValid() ? OK : ERR_SOCKET_NOT_CONNECTED;
}

void TCPSocketGtos::Close() {
  accept_timer_.Stop();
  connect_timer_.Stop();
  read_timer_.Stop();
  write_timer_.Stop();
  accept_ = {};
  connect_callback_.Reset();
  read_ = {};
  write_ = {};
  readiness_only_ = false;
  if (IsValid()) {
    if (physical_) {
      Call(GTOS_SYS_TCP_CLOSE,
           GtosTcpControlRequest{GTOS_TCP_ABI_VERSION,
                                 static_cast<unsigned>(handle_)});
    } else {
      Control(GTOS_SYS_LOOPBACK_CLOSE, handle_);
    }
    handle_ = 0;
  }
  physical_ = false;
  loopback_in_use_ = false;
}

void TCPSocketGtos::DetachFromThread() {
  CHECK(!accept_.callback && !connect_callback_ && !read_.callback &&
        !write_.callback);
  accept_timer_.Stop();
  connect_timer_.Stop();
  read_timer_.Stop();
  write_timer_.Stop();
}

}  // namespace net
