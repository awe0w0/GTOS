#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <process/abi.h>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/cache_type.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_handle.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_status_code.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace {

template <unsigned Size>
void Say(const char (&message)[Size]) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call)
                 : "b"(message), "c"(Size - 1) : "cc", "memory");
}

class PageServer : public net::HttpServer::Delegate {
public:
    int Listen() {
        auto socket = std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource{});
        const int error = socket->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0), 2, {});
        if (error != net::OK) return error;
        server_ = std::make_unique<net::HttpServer>(std::move(socket), this);
        return server_->GetLocalAddress(&address_);
    }

    unsigned port() const { return address_.port(); }
    const std::string& body() const { return body_; }
    unsigned page_requests() const { return page_requests_; }
    unsigned redirects() const { return redirects_; }
    unsigned unexpected_requests() const { return unexpected_requests_; }

private:
    void OnConnect(int) override { Say("GTOS CHROMIUM HTTP ACCEPT\n"); }
    void OnClose(int) override {}

    void OnWebSocketRequest(int connection, const net::HttpServerRequestInfo&) override {
        ++unexpected_requests_;
        server_->Close(connection);
    }

    void OnWebSocketMessage(int connection, std::string) override {
        ++unexpected_requests_;
        server_->Close(connection);
    }

    void OnHttpRequest(int connection, const net::HttpServerRequestInfo& request) override {
        if (request.method != "GET" || !request.peer.address().IsLoopback() ||
            request.GetHeaderValue("host") != "localhost:" + std::to_string(port())) {
            ++unexpected_requests_;
            server_->Send404(connection, TRAFFIC_ANNOTATION_FOR_TESTS);
            return;
        }
        if (request.path == "/start") {
            ++redirects_;
            Say("GTOS CHROMIUM HTTP SERVE REDIRECT\n");
            net::HttpServerResponseInfo response(net::HTTP_FOUND);
            response.AddHeader("Location", "/page?followed=1");
            response.SetBody("", "text/plain");
            server_->SendResponse(connection, response, TRAFFIC_ANNOTATION_FOR_TESTS);
        } else if (request.path == "/page?followed=1") {
            ++page_requests_;
            Say("GTOS CHROMIUM HTTP SERVE DOCUMENT\n");
            server_->Send200(connection, body_, "text/html", TRAFFIC_ANNOTATION_FOR_TESTS);
        } else {
            ++unexpected_requests_;
            server_->Send404(connection, TRAFFIC_ANNOTATION_FOR_TESTS);
        }
    }

    net::IPEndPoint address_;
    std::unique_ptr<net::HttpServer> server_;
    const std::string body_ = "<!doctype html><title>GTOS HTTP</title>" + std::string(5000, 'A');
    unsigned redirects_ = 0;
    unsigned page_requests_ = 0;
    unsigned unexpected_requests_ = 0;
};

class PageRequest : public net::URLRequest::Delegate {
public:
    int Get(const net::URLRequestContext& context, const GURL& url) {
        auto request = context.CreateRequest(url, net::DEFAULT_PRIORITY, this,
            TRAFFIC_ANNOTATION_FOR_TESTS, net::handles::kInvalidNetworkHandle);
        request->Start();
        Say("GTOS CHROMIUM HTTP START RETURNED\n");

        // The guest deadline covers asynchronous work after Start returns.
        // The host guard also bounds synchronous startup and shutdown.
        base::OneShotTimer deadline;
        deadline.Start(FROM_HERE, base::Minutes(2), base::BindOnce([](base::OnceClosure quit) {
            Say("GTOS CHROMIUM HTTP REQUEST DEADLINE\n");
            std::move(quit).Run();
        }, loop_.QuitClosure()));
        loop_.Run();
        final_url_ = request->url();
        return result_;
    }

    int code() const { return code_; }
    const GURL& final_url() const { return final_url_; }
    const std::string& mime_type() const { return mime_type_; }
    const std::string& bytes() const { return bytes_; }

private:
    void OnResponseStarted(net::URLRequest* request, int error) override {
        if (error != net::OK) {
            Say("GTOS CHROMIUM HTTP RESPONSE ERROR\n");
            Finish(error);
            return;
        }
        Say("GTOS CHROMIUM HTTP RESPONSE STARTED\n");
        code_ = request->GetResponseCode();
        request->GetMimeType(&mime_type_);
        Read(request, request->Read(buffer_.get(), buffer_->size()));
    }

    void OnReadCompleted(net::URLRequest* request, int count) override {
        Read(request, count);
    }

    void Read(net::URLRequest* request, int count) {
        while (count > 0) {
            if (count > buffer_->size() || bytes_.size() + count > 8192) {
                Finish(net::ERR_FILE_TOO_BIG);
                return;
            }
            bytes_.append(buffer_->data(), count);
            count = request->Read(buffer_.get(), buffer_->size());
        }
        if (count != net::ERR_IO_PENDING) Finish(count);
    }

    void Finish(int result) {
        result_ = result;
        loop_.Quit();
    }

    base::RunLoop loop_;
    scoped_refptr<net::IOBufferWithSize> buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(251);
    std::string bytes_;
    GURL final_url_;
    std::string mime_type_;
    int result_ = net::ERR_TIMED_OUT;
    int code_ = -1;
};

int Run() {
#ifdef GTOS_HTTP_SEPARATE_FIXTURE
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.SetSpdyAndQuicEnabled(false, false);
    auto context = builder.Build();
    const std::string origin = "http://localhost:18761";
    PageRequest missing;
    if (missing.Get(*context, GURL(origin + "/missing")) != net::OK ||
        missing.code() != net::HTTP_NOT_FOUND || !missing.bytes().empty()) return 211;
    PageRequest document;
    if (document.Get(*context, GURL(origin + "/start")) != net::OK ||
        document.code() != net::HTTP_OK || document.mime_type() != "text/html" ||
        document.final_url() != GURL(origin + "/page?from=redirect") ||
        document.bytes().find("<title>GTOS loopback HTTP test</title>") == std::string::npos ||
        document.bytes().find("replaceChildren(marker)") == std::string::npos) return 212;
    Say("GTOS CHROMIUM SEPARATE HTTP CLIENT PASS\n");
    return 0;
#else
    Say("GTOS CHROMIUM HTTP LISTEN BEGIN\n");
    PageServer server;
    const int listening = server.Listen();
    if (listening == net::ERR_NOT_IMPLEMENTED) {
        Say("GTOS CHROMIUM HTTP DISABLED\n");
        return 45;
    }
    if (listening != net::OK || server.port() < 49152) return 201;
    Say("GTOS CHROMIUM HTTP LISTEN READY\n");

    const base::FilePath cache_path("/legacy-http-cache");
    bool callback_ran = false;
    auto cache = disk_cache::CreateCacheBackend(net::DISK_CACHE, net::CACHE_BACKEND_BLOCKFILE,
        nullptr, cache_path, 8192, disk_cache::ResetHandling::kNeverReset, nullptr, nullptr,
        base::BindOnce([](bool* called, disk_cache::BackendResult) { *called = true; },
                       &callback_ran));
    if (cache.net_error != net::ERR_NOT_IMPLEMENTED || cache.backend || callback_ran ||
        base::SysInfo::AmountOfDiskSpace(cache_path) ||
        base::SysInfo::AmountOfTotalDiskSpace(cache_path) ||
        base::SysInfo::AmountOfFreeDiskSpace(cache_path) || std::atol(" -2049;") != -2049) return 207;

    net::HttpAuthPreferences auth;
    const url::SchemeHostPort own(GURL("https://localhost/"));
    const url::SchemeHostPort other(GURL("https://gtos-fixture.invalid/"));
    if (auth.CanUseDefaultCredentials(own) ||
        auth.GetDelegationType(own) != net::HttpAuth::DelegationType::kNone) return 208;
    auth.SetServerAllowlist("localhost");
    if (!auth.CanUseDefaultCredentials(own) || auth.CanUseDefaultCredentials(other) ||
        auth.GetDelegationType(own) != net::HttpAuth::DelegationType::kNone) return 209;

    Say("GTOS CHROMIUM HTTP CONTEXT BEGIN\n");
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.SetSpdyAndQuicEnabled(false, false);
    auto context = builder.Build();

    const std::string origin = "http://localhost:" + std::to_string(server.port());
    PageRequest page;
    Say("GTOS CHROMIUM HTTP GET BEGIN\n");
    if (page.Get(*context, GURL(origin + "/start")) != net::OK) return 202;
    if (page.code() != net::HTTP_OK || page.mime_type() != "text/html" ||
        page.final_url() != GURL(origin + "/page?followed=1") ||
        page.bytes() != server.body() || server.page_requests() != 1 ||
        server.redirects() != 1 || server.unexpected_requests() != 0) return 203;
    Say("GTOS CHROMIUM HTTP REDIRECT BODY PASS\n");

    PageRequest external;
    if (external.Get(*context, GURL("http://192.0.2.47:" + std::to_string(server.port()) + "/")) !=
        net::ERR_ADDRESS_UNREACHABLE) return 204;
    PageRequest unresolved;
    const int dns = unresolved.Get(*context, GURL("http://gtos-fixture.invalid/"));
    if (dns != net::ERR_NAME_NOT_RESOLVED && dns != net::ERR_NAME_RESOLUTION_FAILED) return 205;
    if (!external.bytes().empty() || !unresolved.bytes().empty() ||
        server.page_requests() != 1 || server.redirects() != 1 ||
        server.unexpected_requests() != 0) return 206;
    Say("GTOS CHROMIUM HTTP NONLOCAL REFUSED\n");
    return 37;
#endif
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    Say("GTOS CHROMIUM HTTP MAIN\n");
    base::AtExitManager at_exit;
    base::CommandLine::Init(argc, argv);
    base::FeatureList::InitInstance("", "");
    base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
    Say("GTOS CHROMIUM HTTP THREAD POOL BEGIN\n");
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("GtosHttp");
    const int result = Run();
    Say("GTOS CHROMIUM HTTP SHUTDOWN BEGIN\n");
    base::ThreadPoolInstance::Get()->Shutdown();
    return result;
}
