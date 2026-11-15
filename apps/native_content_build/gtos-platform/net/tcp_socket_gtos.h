// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TCP_SOCKET_GTOS_H_
#define NET_SOCKET_TCP_SOCKET_GTOS_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "net/base/address_family.h"
#include "net/base/completion_once_callback.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/base/network_handle.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/socket_descriptor.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class AddressList;
class IOBuffer;
class IPEndPoint;
class SocketTag;

// Local sockets use the loopback endpoint. Other IPv4 connections use the
// TCP endpoint when the current boot profile supplies it.
class NET_EXPORT TCPSocketGtos final {
 public:
  static std::unique_ptr<TCPSocketGtos> Create(
      std::unique_ptr<SocketPerformanceWatcher> watcher,
      net::NetLog* net_log,
      const NetLogSource&);
  static std::unique_ptr<TCPSocketGtos> Create(
      std::unique_ptr<SocketPerformanceWatcher> watcher,
      NetLogWithSource net_log);

  TCPSocketGtos(const TCPSocketGtos&) = delete;
  TCPSocketGtos& operator=(const TCPSocketGtos&) = delete;
  ~TCPSocketGtos();

  int Open(AddressFamily family);
  int AdoptConnectedSocket(SocketDescriptor, const IPEndPoint&) {
    return ERR_NOT_IMPLEMENTED;
  }
  int AdoptUnconnectedSocket(SocketDescriptor) { return ERR_NOT_IMPLEMENTED; }
  int BindToNetwork(handles::NetworkHandle) { return ERR_NOT_IMPLEMENTED; }
  int Bind(const IPEndPoint& address);
  int Listen(int backlog);
  int Accept(std::unique_ptr<TCPSocketGtos>* socket, IPEndPoint* address,
             CompletionOnceCallback callback);
  int Connect(const IPEndPoint& address, CompletionOnceCallback callback);

  int Read(IOBuffer* buffer, int length, CompletionOnceCallback callback);
  int ReadIfReady(IOBuffer* buffer, int length, CompletionOnceCallback callback);
  int CancelReadIfReady();
  int Write(IOBuffer* buffer, int length, CompletionOnceCallback callback,
            const NetworkTrafficAnnotationTag&);
  int GetLocalAddress(IPEndPoint* address) const;
  int GetPeerAddress(IPEndPoint* address) const;

  int SetDefaultOptionsForServer();
  void SetDefaultOptionsForClient() {}
  int AllowAddressReuse() { return ERR_NOT_IMPLEMENTED; }
  int SetReceiveBufferSize(int32_t) { return ERR_NOT_IMPLEMENTED; }
  int SetSendBufferSize(int32_t) { return ERR_NOT_IMPLEMENTED; }
  bool SetKeepAlive(bool, int) { return false; }
  bool SetNoDelay(bool enabled) { return IsValid(); }
  int SetIPv6Only(bool) { return ERR_NOT_IMPLEMENTED; }

  [[nodiscard]] bool GetEstimatedRoundTripTime(base::TimeDelta*) const {
    return false;
  }
  bool IsValid() const { return handle_ > 0; }
  bool IsConnected() const;
  bool IsConnectedAndIdle() const;
  void Close();
  void DetachFromThread();
  void ApplySocketTag(const SocketTag&) {}

  void StartLoggingMultipleConnectAttempts(const AddressList&) {
    net_log_.BeginEvent(NetLogEventType::TCP_CONNECT);
  }
  void EndLoggingMultipleConnectAttempts(int error) {
    net_log_.EndEventWithNetErrorCode(NetLogEventType::TCP_CONNECT, error);
  }
  const NetLogWithSource& net_log() const { return net_log_; }
  SocketPerformanceWatcher* socket_performance_watcher() const {
    return watcher_.get();
  }
  SocketDescriptor SocketDescriptorForTesting() const { return kInvalidSocket; }
  SocketDescriptor ReleaseSocketDescriptorForTesting() { return kInvalidSocket; }

 private:
  struct SocketInfo;
  TCPSocketGtos(std::unique_ptr<SocketPerformanceWatcher> watcher,
                NetLogWithSource net_log);
  int GetSocketInfo(SocketInfo& info) const;
  int TryAccept(std::unique_ptr<TCPSocketGtos>* socket, IPEndPoint* address);
  int TryTransfer(bool reading, IOBuffer* buffer, int length);
  void Arm(base::OneShotTimer& timer, void (TCPSocketGtos::*method)());
  int ReadInternal(IOBuffer* buffer, int length, CompletionOnceCallback callback,
                   bool readiness_only);
  void CompleteAccept();
  void CompleteConnect();
  void CompleteRead();
  void CompleteWrite();

  std::unique_ptr<SocketPerformanceWatcher> watcher_;
  NetLogWithSource net_log_;
  int handle_ = 0;
  bool physical_ = false;
  bool loopback_in_use_ = false;
  CompletionOnceCallback connect_callback_;
  struct AcceptOperation {
    raw_ptr<std::unique_ptr<TCPSocketGtos>> result = nullptr;
    raw_ptr<IPEndPoint> address = nullptr;
    CompletionOnceCallback callback;
  } accept_;
  struct TransferOperation {
    scoped_refptr<IOBuffer> buffer;
    int length = 0;
    CompletionOnceCallback callback;
  } read_, write_;
  bool readiness_only_ = false;
  base::OneShotTimer accept_timer_, connect_timer_, read_timer_, write_timer_;
};

}  // namespace net

#endif  // NET_SOCKET_TCP_SOCKET_GTOS_H_
