// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_UDP_SOCKET_GTOS_H_
#define NET_SOCKET_UDP_SOCKET_GTOS_H_

#include "base/functional/callback.h"
#include "net/base/address_family.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/datagram_client_socket.h"
#include "net/socket/socket_descriptor.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

// GTOS has address values but no native datagram endpoint. A socket remains
// closed, and operations complete synchronously with an error.
class UDPSocketGtos final {
 public:
  UDPSocketGtos(DatagramSocket::BindType,
                net::NetLog* net_log,
                const NetLogSource&)
      : net_log_(NetLogWithSource::Make(net_log, NetLogSourceType::UDP_SOCKET)) {}
  UDPSocketGtos(DatagramSocket::BindType, NetLogWithSource net_log)
      : net_log_(net_log) {}

  UDPSocketGtos(const UDPSocketGtos&) = delete;
  UDPSocketGtos& operator=(const UDPSocketGtos&) = delete;

  int Open(AddressFamily) { return ERR_NOT_IMPLEMENTED; }
  int AdoptOpenedSocket(AddressFamily, SocketDescriptor) {
    return ERR_NOT_IMPLEMENTED;
  }
  int BindToNetwork(handles::NetworkHandle) { return ERR_NOT_IMPLEMENTED; }
  int Bind(const IPEndPoint&) { return ERR_SOCKET_NOT_CONNECTED; }
  int Connect(const IPEndPoint&) { return ERR_SOCKET_NOT_CONNECTED; }
  int GetPeerAddress(IPEndPoint*) const { return ERR_SOCKET_NOT_CONNECTED; }
  int GetLocalAddress(IPEndPoint*) const { return ERR_SOCKET_NOT_CONNECTED; }

  int Read(IOBuffer*, int, CompletionOnceCallback) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  base::expected<DatagramsMetadata, Error> ReadMultiple(
      IOBuffer*, size_t, size_t,
      base::OnceCallback<void(base::expected<DatagramsMetadata, Error>)>) {
    return base::unexpected(ERR_SOCKET_NOT_CONNECTED);
  }
  int Write(IOBuffer*, int, CompletionOnceCallback,
            const NetworkTrafficAnnotationTag&) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  int RecvFrom(IOBuffer*, int, IPEndPoint*, CompletionOnceCallback) {
    return ERR_SOCKET_NOT_CONNECTED;
  }
  int SendTo(IOBuffer*, int, const IPEndPoint&, CompletionOnceCallback) {
    return ERR_SOCKET_NOT_CONNECTED;
  }

  int SetReceiveBufferSize(int32_t) { return ERR_NOT_IMPLEMENTED; }
  int SetSendBufferSize(int32_t) { return ERR_NOT_IMPLEMENTED; }
  int SetIPv6Only(bool) { return ERR_NOT_IMPLEMENTED; }
  int SetDoNotFragment() { return ERR_NOT_IMPLEMENTED; }
  int SetRecvTos() { return ERR_NOT_IMPLEMENTED; }
  int SetTos(DiffServCodePoint, EcnCodePoint) { return ERR_NOT_IMPLEMENTED; }
  int SetDiffServCodePoint(DiffServCodePoint) { return ERR_NOT_IMPLEMENTED; }
  int AllowAddressReuse() { return ERR_NOT_IMPLEMENTED; }
  int SetBroadcast(bool) { return ERR_NOT_IMPLEMENTED; }
  int AllowAddressSharingForMulticast() { return ERR_NOT_IMPLEMENTED; }
  int JoinGroup(const IPAddress&) const { return ERR_NOT_IMPLEMENTED; }
  int LeaveGroup(const IPAddress&) const { return ERR_NOT_IMPLEMENTED; }
  int JoinSourceGroup(const IPAddress&, const IPAddress&) const {
    return ERR_NOT_IMPLEMENTED;
  }
  int LeaveSourceGroup(const IPAddress&, const IPAddress&) const {
    return ERR_NOT_IMPLEMENTED;
  }
  int SetMulticastInterface(uint32_t) { return ERR_NOT_IMPLEMENTED; }
  int SetMulticastTimeToLive(int) { return ERR_NOT_IMPLEMENTED; }
  int SetMulticastLoopbackMode(bool) { return ERR_NOT_IMPLEMENTED; }

  void Close() {}
  void SetMsgConfirm(bool) {}
  void ApplySocketTag(const SocketTag&) {}
  void DetachFromThread() {}

  const NetLogWithSource& NetLog() const { return net_log_; }
  DscpAndEcn GetLastTos() const { return TosToDscpAndEcn(0); }
  uint32_t get_multicast_interface_for_testing() const { return 0; }
  bool get_msg_confirm_for_testing() const { return false; }
  bool get_experimental_recv_optimization_enabled_for_testing() const {
    return false;
  }

 private:
  NetLogWithSource net_log_;
};

}  // namespace net

#endif  // NET_SOCKET_UDP_SOCKET_GTOS_H_
