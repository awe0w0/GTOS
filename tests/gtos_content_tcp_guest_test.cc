#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <process/abi.h>
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/address_list.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_handle.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_source.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"

namespace {

template <unsigned Size>
void Say(const char (&message)[Size]) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call)
                 : "b"(message), "c"(Size - 1) : "cc", "memory");
}

class Completion {
public:
    net::CompletionOnceCallback callback() {
        return base::BindOnce(&Completion::Complete, base::Unretained(this));
    }

    int Wait(int immediate) {
        if (immediate != net::ERR_IO_PENDING) return immediate;
        if (result_ == net::ERR_IO_PENDING) {
            base::OneShotTimer timeout;
            timeout.Start(FROM_HERE, base::Seconds(5), loop_.QuitClosure());
            loop_.Run();
        }
        return result_ == net::ERR_IO_PENDING ? net::ERR_TIMED_OUT : result_;
    }

private:
    void Complete(int result) {
        result_ = result;
        loop_.Quit();
    }

    base::RunLoop loop_;
    int result_ = net::ERR_IO_PENDING;
};

int Run() {
    constexpr std::string_view request =
        "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    constexpr std::string_view body = "<title>GTOS loopback</title>";
    constexpr std::string_view response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
        "<title>GTOS loopback</title>";
    auto local = net::IPAddress::IPv4Localhost();
    Completion accept;
    net::TCPServerSocket server(nullptr, {});
    const int listening = server.Listen(net::IPEndPoint(local, 0), 2, {});
    if (listening == net::ERR_NOT_IMPLEMENTED) {
        Say("GTOS CHROMIUM TCP DISABLED\n");
        return 45;
    }
    net::IPEndPoint bound, origin, peer;
    if (listening != net::OK || server.GetLocalAddress(&bound) != net::OK ||
        bound.address() != local || bound.port() < 49152) return 201;

    net::TCPClientSocket rejected(
        net::AddressList(net::IPEndPoint(net::IPAddress(192, 0, 2, 47), bound.port())),
        nullptr, nullptr, nullptr, {}, net::handles::kInvalidNetworkHandle);
    Completion deny;
    if (deny.Wait(rejected.Connect(deny.callback())) != net::ERR_ADDRESS_UNREACHABLE)
        return 202;

    std::unique_ptr<net::StreamSocket> stream;
    const int waiting = server.Accept(&stream, accept.callback(), &peer);
    if (waiting != net::ERR_IO_PENDING || stream) return 203;
    net::TCPClientSocket client(net::AddressList(std::vector<net::IPEndPoint>{
        net::IPEndPoint(net::IPAddress::IPv6Localhost(), bound.port()), bound}),
        nullptr, nullptr, nullptr, {}, net::handles::kInvalidNetworkHandle);
    Completion connect;
    if (connect.Wait(client.Connect(connect.callback())) != net::OK ||
        accept.Wait(waiting) != net::OK || !stream ||
        client.GetLocalAddress(&origin) != net::OK || origin != peer ||
        !client.IsConnectedAndIdle()) return 204;

    auto read = base::MakeRefCounted<net::IOBufferWithSize>(8192);
    int canceled_callbacks = 0;
    if (client.ReadIfReady(read.get(), 8192, base::BindOnce(
            [](int* count, int) { ++*count; }, &canceled_callbacks)) != net::ERR_IO_PENDING ||
        client.CancelReadIfReady() != net::OK) return 205;
    Completion request_read;
    const int reading = stream->Read(read.get(), 8192, request_read.callback());
    auto request_bytes = base::MakeRefCounted<net::StringIOBuffer>(std::string(request));
    Completion request_write;
    if (reading != net::ERR_IO_PENDING || request_write.Wait(client.Write(
            request_bytes.get(), request.size(), request_write.callback(),
            TRAFFIC_ANNOTATION_FOR_TESTS)) != static_cast<int>(request.size()) ||
        request_read.Wait(reading) != static_cast<int>(request.size()) ||
        std::string_view(read->data(), request.size()) != request ||
        canceled_callbacks != 0) return 206;

    Completion ready;
    const int idle = client.ReadIfReady(read.get(), 8192, ready.callback());
    auto response_bytes = base::MakeRefCounted<net::StringIOBuffer>(std::string(response));
    Completion response_write;
    if (idle != net::ERR_IO_PENDING || response_write.Wait(stream->Write(
            response_bytes.get(), response.size(), response_write.callback(),
            TRAFFIC_ANNOTATION_FOR_TESTS)) != static_cast<int>(response.size()) ||
        ready.Wait(idle) != net::OK || canceled_callbacks != 0) return 207;
    Completion read_http;
    if (read_http.Wait(client.Read(read.get(), 8192, read_http.callback())) !=
        static_cast<int>(response.size())) return 208;
    std::string_view bytes(read->data(), response.size());
    const auto boundary = bytes.find("\r\n\r\n");
    if (boundary == std::string_view::npos || bytes.substr(boundary + 4) != body) return 209;
    auto headers = net::HttpResponseHeaders::TryToCreate(bytes.substr(0, boundary + 4));
    if (!headers || headers->response_code() != 200 ||
        !headers->HasHeaderValue("Content-Type", "text/html")) return 210;

    auto full = base::MakeRefCounted<net::StringIOBuffer>(std::string(4097, '*'));
    Completion fill, resume, drain;
    if (fill.Wait(stream->Write(full.get(), 4097, fill.callback(),
            TRAFFIC_ANNOTATION_FOR_TESTS)) != 4096) return 211;
    const int backpressure = stream->Write(full.get(), 1, resume.callback(),
                                          TRAFFIC_ANNOTATION_FOR_TESTS);
    if (backpressure != net::ERR_IO_PENDING ||
        drain.Wait(client.Read(read.get(), 4096, drain.callback())) != 4096 ||
        std::string_view(read->data(), 4096) != std::string_view(full->data(), 4096) ||
        resume.Wait(backpressure) != 1) return 212;
    stream.reset();
    Completion last, eof;
    if (last.Wait(client.Read(read.get(), 8192, last.callback())) != 1 ||
        read->data()[0] != '*' ||
        eof.Wait(client.Read(read.get(), 8192, eof.callback())) != 0 ||
        client.IsConnected()) return 213;
    Say("GTOS CHROMIUM TCP HTTP BYTES PASS\n");
    return 37;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    base::CommandLine::Init(argc, argv);
    base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
    return Run();
}
