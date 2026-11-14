// Exercises the Chromium URLRequest TLS client on the native GTOS loopback ABI.
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/x509.h>
#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_handle.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_certificate.h"
#include "net/http/http_status_code.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/socket/server_socket.h"
#include "net/socket/ssl_server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/ssl/ssl_config.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "net/ssl/ssl_server_config.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"

namespace {

constexpr std::string_view kPage = "<!doctype html><title>GTOS HTTPS</title>";

template <size_t Size>
void Say(const char (&message)[Size]) {
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call)
                 : "b"(message), "c"(Size - 1) : "cc", "memory");
}

void Trace(std::string_view label, int result) {
    char message[128];
    const int length = std::snprintf(message, sizeof(message), "GTOS HTTPS %.*s %d\n",
        static_cast<int>(label.size()), label.data(), result);
    if (length <= 0 || length >= static_cast<int>(sizeof(message))) return;
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call)
                 : "b"(message), "c"(length) : "cc", "memory");
}

class TlsEvents final : public net::NetLog::ThreadSafeObserver {
public:
    void OnAddEntry(const net::NetLogEntry& entry) override {
        const char* label;
        switch (entry.type) {
            case net::NetLogEventType::SSL_CONNECT: label = "client TLS"; break;
            case net::NetLogEventType::SSL_SERVER_HANDSHAKE: label = "server TLS"; break;
            case net::NetLogEventType::SSL_CERTIFICATES_RECEIVED: label = "certificate received"; break;
            case net::NetLogEventType::CERT_VERIFIER_TASK: label = "certificate task"; break;
            case net::NetLogEventType::CERT_VERIFY_PROC: label = "certificate path validation"; break;
            default: return;
        }
        Trace(label, static_cast<int>(entry.phase));
    }
};

class ObserveTlsEvents {
public:
    ObserveTlsEvents() {
        net::NetLog::Get()->AddObserver(&observer_, net::NetLogCaptureMode::kHeavilyRedacted);
    }
    ~ObserveTlsEvents() { net::NetLog::Get()->RemoveObserver(&observer_); }

private:
    TlsEvents observer_;
};

bssl::UniquePtr<EVP_PKEY> MakeKey() {
    bssl::UniquePtr<EVP_PKEY_CTX> context(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
    EVP_PKEY* key = nullptr;
    if (!context || !EVP_PKEY_keygen_init(context.get()) ||
        !EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) ||
        !EVP_PKEY_keygen(context.get(), &key)) return nullptr;
    return bssl::UniquePtr<EVP_PKEY>(key);
}

bool AddExtension(X509* certificate, int id, const char* value) {
    bssl::UniquePtr<X509_EXTENSION> extension(
        X509V3_EXT_nconf_nid(nullptr, nullptr, id, value));
    return extension && X509_add_ext(certificate, extension.get(), -1);
}

scoped_refptr<net::X509Certificate> MakeCertificate(
    EVP_PKEY* key, EVP_PKEY* issuer_key, const char* name,
    const X509_NAME* issuer, unsigned serial, bool expired) {
    bssl::UniquePtr<X509> certificate(X509_new());
    if (!certificate || !X509_set_version(certificate.get(), 2) ||
        !ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), serial) ||
        !ASN1_TIME_set_string(X509_getm_notBefore(certificate.get()), "250101000000Z") ||
        !ASN1_TIME_set_string(X509_getm_notAfter(certificate.get()),
                              expired ? "250201000000Z" : "270101000000Z") ||
        !X509_set_pubkey(certificate.get(), key)) return nullptr;

    auto* subject = X509_get_subject_name(certificate.get());
    const bool root = !issuer;
    if (!X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
            reinterpret_cast<const uint8_t*>(name), -1, -1, 0) ||
        !X509_set_issuer_name(certificate.get(), root ? subject : issuer) ||
        !AddExtension(certificate.get(), NID_basic_constraints,
                      root ? "critical,CA:TRUE" : "critical,CA:FALSE") ||
        !AddExtension(certificate.get(), NID_key_usage,
                      root ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature") ||
        (!root && (!AddExtension(certificate.get(), NID_subject_alt_name, "DNS:localhost") ||
                   !AddExtension(certificate.get(), NID_ext_key_usage, "serverAuth"))) ||
        !X509_sign(certificate.get(), issuer_key, EVP_sha256())) return nullptr;

    uint8_t* der = nullptr;
    const int size = i2d_X509(certificate.get(), &der);
    std::unique_ptr<uint8_t, decltype(&OPENSSL_free)> encoded(der, &OPENSSL_free);
    if (size <= 0 || !encoded) return nullptr;
    return net::X509Certificate::CreateFromBytes(
        UNSAFE_BUFFERS(base::span(encoded.get(), static_cast<size_t>(size))));
}

// HttpServer receives a connected TLS socket only after its handshake completes.
class TlsListener final : public net::ServerSocket {
public:
    explicit TlsListener(std::unique_ptr<net::SSLServerContext> context)
        : context_(std::move(context)) {}

    int Listen(const net::IPEndPoint& address, int backlog,
               std::optional<bool> ipv6_only) override {
        return tcp_.Listen(address, backlog, ipv6_only);
    }

    int GetLocalAddress(net::IPEndPoint* address) const override {
        return tcp_.GetLocalAddress(address);
    }

    int Accept(std::unique_ptr<net::StreamSocket>* output,
               net::CompletionOnceCallback callback) override {
        output_ = output;
        const int result = tcp_.Accept(&accepted_, base::BindOnce(
            &TlsListener::OnTcpAccepted, weak_.GetWeakPtr()));
        if (result == net::ERR_IO_PENDING) {
            callback_ = std::move(callback);
            return result;
        }
        const int handshake = StartHandshake(result);
        if (handshake == net::ERR_IO_PENDING) {
            callback_ = std::move(callback);
        } else {
            CompleteSocket(handshake);
        }
        return handshake;
    }

private:
    int StartHandshake(int result) {
        if (result != net::OK) return result;
        secure_ = context_->CreateSSLServerSocket(std::move(accepted_));
        return secure_->Handshake(base::BindOnce(&TlsListener::OnHandshake, weak_.GetWeakPtr()));
    }

    void OnTcpAccepted(int result) {
        const int handshake = StartHandshake(result);
        if (handshake != net::ERR_IO_PENDING) OnHandshake(handshake);
    }

    void CompleteSocket(int result) {
        if (result == net::OK) {
            *output_ = std::move(secure_);
        } else {
            secure_.reset();
        }
        output_ = nullptr;
    }

    void OnHandshake(int result) {
        CompleteSocket(result);
        std::move(callback_).Run(result);
    }

    std::unique_ptr<net::SSLServerContext> context_;
    net::TCPServerSocket tcp_{nullptr, {}};
    std::unique_ptr<net::StreamSocket> accepted_;
    std::unique_ptr<net::SSLServerSocket> secure_;
    raw_ptr<std::unique_ptr<net::StreamSocket>> output_ = nullptr;
    net::CompletionOnceCallback callback_;
    base::WeakPtrFactory<TlsListener> weak_{this};
};

class PageServer final : public net::HttpServer::Delegate {
public:
    int Listen(net::X509Certificate& certificate, EVP_PKEY& key) {
        net::SSLServerConfig config;
        config.version_min = config.version_max = net::SSL_PROTOCOL_VERSION_TLS1_3;
        auto listener = std::make_unique<TlsListener>(
            net::CreateSSLServerContext(&certificate, &key, config));
        const int result = listener->Listen(
            net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0), 2, {});
        if (result != net::OK) return result;
        server_ = std::make_unique<net::HttpServer>(std::move(listener), this);
        return server_->GetLocalAddress(&address_);
    }

    uint16_t port() const { return address_.port(); }
    unsigned served() const { return served_; }
    unsigned unexpected() const { return unexpected_; }

private:
    void OnConnect(int) override {}
    void OnClose(int) override {}

    void OnWebSocketRequest(int id, const net::HttpServerRequestInfo&) override {
        ++unexpected_;
        server_->Close(id);
    }

    void OnWebSocketMessage(int id, std::string) override {
        ++unexpected_;
        server_->Close(id);
    }

    void OnHttpRequest(int id, const net::HttpServerRequestInfo& request) override {
        if (request.method != "GET" || request.path != "/index.html" ||
            !request.peer.address().IsLoopback() ||
            request.GetHeaderValue("host") != "localhost:" + std::to_string(port())) {
            ++unexpected_;
            server_->Send404(id, TRAFFIC_ANNOTATION_FOR_TESTS);
            return;
        }
        ++served_;
        server_->Send200(id, kPage, "text/html", TRAFFIC_ANNOTATION_FOR_TESTS);
    }

    std::unique_ptr<net::HttpServer> server_;
    net::IPEndPoint address_;
    unsigned served_ = 0;
    unsigned unexpected_ = 0;
};

class PageRequest final : public net::URLRequest::Delegate {
public:
    int Get(const net::URLRequestContext& context, const GURL& url) {
        auto request = context.CreateRequest(url, net::DEFAULT_PRIORITY, this,
            TRAFFIC_ANNOTATION_FOR_TESTS, net::handles::kInvalidNetworkHandle);
        request->Start();
        Say("GTOS CHROMIUM HTTPS START RETURNED\n");
        base::OneShotTimer deadline;
        deadline.Start(FROM_HERE, base::Minutes(2),
            base::BindOnce(&PageRequest::Finish, base::Unretained(this), net::ERR_TIMED_OUT));
        loop_.Run();
        return result_;
    }

    bool verified_page() const { return verified_page_ && body_ == kPage; }
    int certificate_error() const { return certificate_error_; }

private:
    void OnSSLCertificateError(net::URLRequest* request, int error,
                               const net::SSLInfo&, bool) override {
        Trace("certificate result", error);
        certificate_error_ = error;
        Trace("canceled status", request->CancelWithError(error));
    }

    void OnResponseStarted(net::URLRequest* request, int error) override {
        Trace("response started", error);
        if (error != net::OK) {
            Finish(error);
            return;
        }
        const auto& info = request->ssl_info();
        verified_page_ = request->GetResponseCode() == net::HTTP_OK && info.cert &&
            !net::IsCertStatusError(info.cert_status) &&
            net::SSLConnectionStatusToVersion(info.connection_status) ==
                net::SSL_CONNECTION_VERSION_TLS1_3;
        Read(request, request->Read(buffer_.get(), buffer_->size()));
    }

    void OnReadCompleted(net::URLRequest* request, int bytes) override {
        Read(request, bytes);
    }

    void Read(net::URLRequest* request, int bytes) {
        while (bytes > 0) {
            if (bytes > buffer_->size() || body_.size() + bytes > 256) {
                Finish(net::ERR_FILE_TOO_BIG);
                return;
            }
            body_.append(buffer_->data(), bytes);
            bytes = request->Read(buffer_.get(), buffer_->size());
        }
        if (bytes != net::ERR_IO_PENDING) Finish(bytes);
    }

    void Finish(int result) {
        result_ = result;
        loop_.Quit();
    }

    base::RunLoop loop_;
    scoped_refptr<net::IOBufferWithSize> buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(64);
    std::string body_;
    int result_ = net::ERR_TIMED_OUT;
    int certificate_error_ = net::OK;
    bool verified_page_ = false;
};

struct RequestCase {
    std::string_view host;
    bool trusted;
    bool expired;
    int expected;
};

int RunCase(net::X509Certificate& root, net::X509Certificate& certificate,
            EVP_PKEY& key, const RequestCase& test) {
    const base::Time start = base::Time::Now();
    net::ScopedTestRoot trust;
    if (!net::TestRootCerts::GetInstance()->IsEmpty()) return 210;
    if (test.trusted) {
        trust.Reset({scoped_refptr<net::X509Certificate>(&root)});
        if (net::TestRootCerts::GetInstance()->IsEmpty()) return 211;
    }
    // Each fixture has its own trust policy. Keep the default verifier and
    // its cache for the lifetime of that policy, including request teardown.
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.SetSpdyAndQuicEnabled(false, false);
    auto context = builder.Build();
    PageServer server;
    const int listened = server.Listen(certificate, key);
    if (listened != net::OK || !server.port()) return 212;

    PageRequest request;
    const GURL url("https://" + std::string(test.host) + ":" +
                   std::to_string(server.port()) + "/index.html");
    Trace("expected request result", test.expected);
    const int observed = request.Get(*context, url);
    const bool clock_moved_back = base::Time::Now() < start;
    Trace("observed request result", observed);
    Trace("unexpected requests", server.unexpected());
    Trace("clock moved back", clock_moved_back);
    if (observed != test.expected || server.unexpected() || clock_moved_back) return 213;
    if (test.expected == net::OK) {
        if (!request.verified_page() || request.certificate_error() != net::OK ||
            server.served() != 1) return 214;
    } else {
        if (request.verified_page() || request.certificate_error() != test.expected ||
            server.served()) return 215;
    }
    return 0;
}

int Run() {
    Say("GTOS CHROMIUM HTTPS CERTIFICATES BEGIN\n");
    auto root_key = MakeKey();
    auto leaf_key = MakeKey();
    if (!root_key || !leaf_key) return 201;
    auto root = MakeCertificate(root_key.get(), root_key.get(), "GTOS offline test CA", nullptr, 1, false);
    if (!root) return 202;
    bssl::UniquePtr<X509_NAME> issuer(X509_NAME_new());
    if (!issuer || !X509_NAME_add_entry_by_txt(issuer.get(), "CN", MBSTRING_ASC,
            reinterpret_cast<const uint8_t*>("GTOS offline test CA"), -1, -1, 0)) return 203;
    auto valid = MakeCertificate(leaf_key.get(), root_key.get(), "localhost", issuer.get(), 2, false);
    auto expired = MakeCertificate(leaf_key.get(), root_key.get(), "localhost", issuer.get(), 3, true);
    if (!valid || !expired) return 204;
    {
        net::TCPServerSocket socket(nullptr, {});
        const int result = socket.Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0), 1, {});
        if (result == net::ERR_NOT_IMPLEMENTED) {
            Say("GTOS CHROMIUM HTTPS DISABLED\n");
            return 45;
        }
        if (result != net::OK) return 205;
    }

    ObserveTlsEvents tls_events;
    constexpr RequestCase cases[] = {
        {"localhost", false, false, net::ERR_CERT_AUTHORITY_INVALID},
        {"localhost", true, false, net::OK},
        {"127.0.0.1", true, false, net::ERR_CERT_COMMON_NAME_INVALID},
        {"localhost", true, true, net::ERR_CERT_DATE_INVALID},
        {"localhost", false, false, net::ERR_CERT_AUTHORITY_INVALID},
    };
    for (size_t i = 0; i < std::size(cases); ++i) {
        const auto& test = cases[i];
        const int result = RunCase(*root, test.expired ? *expired : *valid,
                                   *leaf_key, test);
        if (result) return result;
        switch (test.expected) {
            case net::OK: Say("GTOS CHROMIUM HTTPS TLS13 BODY PASS\n"); break;
            case net::ERR_CERT_AUTHORITY_INVALID:
                if (i == 0) {
                    Say("GTOS CHROMIUM HTTPS UNTRUSTED REFUSED\n");
                } else {
                    Say("GTOS CHROMIUM HTTPS TRUST REMOVED REFUSED\n");
                }
                break;
            case net::ERR_CERT_COMMON_NAME_INVALID:
                Say("GTOS CHROMIUM HTTPS WRONG NAME REFUSED\n");
                break;
            case net::ERR_CERT_DATE_INVALID:
                Say("GTOS CHROMIUM HTTPS EXPIRED REFUSED\n");
                break;
        }
    }
    return 37;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    base::AtExitManager at_exit;
    base::CommandLine::Init(argc, argv);
    base::FeatureList::InitInstance("", "");
    base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("GtosHttps");
    const int result = Run();
    base::ThreadPoolInstance::Get()->Shutdown();
    return result;
}
