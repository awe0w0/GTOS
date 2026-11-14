// Exercises the Chromium BoringSSL TLS profile with ephemeral guest certificates.
#include <array>
#include <cstdint>
#include <string_view>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bssl::UniquePtr<EVP_PKEY> MakeKey() {
  bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
  EVP_PKEY* key = nullptr;
  if (!ctx || !EVP_PKEY_keygen_init(ctx.get()) ||
      !EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(), NID_X9_62_prime256v1) ||
      !EVP_PKEY_keygen(ctx.get(), &key)) {
    return nullptr;
  }
  return bssl::UniquePtr<EVP_PKEY>(key);
}

bool AddExtension(X509* cert, int id, const char* value) {
  bssl::UniquePtr<X509_EXTENSION> ext(
      X509V3_EXT_nconf_nid(nullptr, nullptr, id, value));
  return ext && X509_add_ext(cert, ext.get(), -1);
}

bssl::UniquePtr<X509> MakeCertificate(
    EVP_PKEY* key,
    EVP_PKEY* issuer_key,
    X509* issuer,
    const char* name,
    bool ca) {
  bssl::UniquePtr<X509> cert(X509_new());
  if (!cert || !X509_set_version(cert.get(), 2) ||
      !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), ca ? 1 : 2) ||
      !ASN1_TIME_set_string(X509_getm_notBefore(cert.get()), "250101000000Z") ||
      !ASN1_TIME_set_string(X509_getm_notAfter(cert.get()), "270101000000Z") ||
      !X509_set_pubkey(cert.get(), key)) {
    return nullptr;
  }
  auto* subject = X509_get_subject_name(cert.get());
  if (!X509_NAME_add_entry_by_txt(
          subject, "CN", MBSTRING_ASC,
          reinterpret_cast<const uint8_t*>(name), -1, -1, 0) ||
      !X509_set_issuer_name(cert.get(), issuer ? X509_get_subject_name(issuer)
                                              : subject)) {
    return nullptr;
  }
  if (!AddExtension(cert.get(), NID_basic_constraints,
                    ca ? "critical,CA:TRUE" : "critical,CA:FALSE") ||
      !AddExtension(cert.get(), NID_key_usage,
                    ca ? "critical,keyCertSign,cRLSign"
                       : "critical,digitalSignature") ||
      (!ca && (!AddExtension(cert.get(), NID_subject_alt_name,
                            "DNS:gtos.test") ||
               !AddExtension(cert.get(), NID_ext_key_usage, "serverAuth"))) ||
      !X509_sign(cert.get(), issuer_key, EVP_sha256())) {
    return nullptr;
  }
  return cert;
}

struct HandshakeCase {
  const char* hostname;
  bool trusted;
  int64_t verification_time;
  long expected_result;
};

// Complete a TLS 1.3 handshake over a bounded BIO pair. No socket is opened.
bool RunHandshake(SSL_CTX* server_ctx,
                  X509* root,
                  const HandshakeCase& test) {
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));
  if (!client_ctx ||
      !SSL_CTX_set_min_proto_version(client_ctx.get(), TLS1_3_VERSION) ||
      !SSL_CTX_set_max_proto_version(client_ctx.get(), TLS1_3_VERSION) ||
      (test.trusted &&
       !X509_STORE_add_cert(SSL_CTX_get_cert_store(client_ctx.get()), root))) {
    return false;
  }
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_PEER, nullptr);
  bssl::UniquePtr<SSL> client(SSL_new(client_ctx.get()));
  bssl::UniquePtr<SSL> server(SSL_new(server_ctx));
  if (!client || !server || !SSL_set1_host(client.get(), test.hostname)) {
    return false;
  }
  X509_VERIFY_PARAM_set_time(SSL_get0_param(client.get()), test.verification_time);
  SSL_set_connect_state(client.get());
  SSL_set_accept_state(server.get());
  BIO* client_end = nullptr;
  BIO* server_end = nullptr;
  if (!BIO_new_bio_pair(&client_end, 2048, &server_end, 2048)) {
    return false;
  }
  SSL_set_bio(client.get(), client_end, client_end);
  SSL_set_bio(server.get(), server_end, server_end);

  for (unsigned i = 0; i < 64; ++i) {
    if (!SSL_is_init_finished(client.get())) {
      const int result = SSL_do_handshake(client.get());
      if (result <= 0) {
        const int error = SSL_get_error(client.get(), result);
        if (error == SSL_ERROR_SSL) {
          const long verification = SSL_get_verify_result(client.get());
          ERR_clear_error();
          return test.expected_result != X509_V_OK &&
                 verification == test.expected_result;
        }
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
          return false;
        }
      }
    }
    if (!SSL_is_init_finished(server.get())) {
      const int result = SSL_do_handshake(server.get());
      if (result <= 0) {
        const int error = SSL_get_error(server.get(), result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
          return false;
        }
      }
    }
    if (SSL_is_init_finished(client.get()) &&
        SSL_is_init_finished(server.get())) {
      constexpr std::string_view request = "GET / HTTP/1.1\r\nHost: gtos.test\r\n\r\n";
      constexpr std::string_view reply =
          "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ngtos";
      std::array<char, 128> bytes{};
      if (test.expected_result != X509_V_OK ||
          SSL_get_verify_result(client.get()) != X509_V_OK ||
          SSL_version(client.get()) != TLS1_3_VERSION ||
          SSL_write(client.get(), request.data(), request.size()) !=
              static_cast<int>(request.size())) {
        return false;
      }
      const int request_size = SSL_read(server.get(), bytes.data(), bytes.size());
      if (request_size != static_cast<int>(request.size()) ||
          std::string_view(bytes.data(), request_size) != request ||
          SSL_write(server.get(), reply.data(), reply.size()) !=
              static_cast<int>(reply.size())) {
        return false;
      }
      const int reply_size = SSL_read(client.get(), bytes.data(), bytes.size());
      return reply_size == static_cast<int>(reply.size()) &&
             std::string_view(bytes.data(), reply_size) == reply;
    }
  }
  return false;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  auto root_key = MakeKey();
  auto leaf_key = MakeKey();
  if (!root_key || !leaf_key) {
    return 201;
  }
  auto root = MakeCertificate(root_key.get(), root_key.get(), nullptr,
                              "GTOS test CA", true);
  if (!root) {
    return 202;
  }
  auto leaf = MakeCertificate(leaf_key.get(), root_key.get(), root.get(),
                              "gtos.test", false);
  bssl::UniquePtr<SSL_CTX> server(SSL_CTX_new(TLS_server_method()));
  if (!leaf || !server ||
      !SSL_CTX_use_certificate(server.get(), leaf.get()) ||
      !SSL_CTX_use_PrivateKey(server.get(), leaf_key.get()) ||
      !SSL_CTX_set_min_proto_version(server.get(), TLS1_3_VERSION) ||
      !SSL_CTX_set_max_proto_version(server.get(), TLS1_3_VERSION)) {
    return 203;
  }
  SSL_CTX_set_num_tickets(server.get(), 0);

  constexpr int64_t valid_time = 1767225600;   // 2026-01-01 UTC.
  constexpr int64_t expired_time = 1830384000; // 2028-01-02 UTC.
  if (!RunHandshake(server.get(), root.get(),
                    {"gtos.test", true, valid_time, X509_V_OK}) ||
      !Say("GTOS TLS13 VERIFIED HANDSHAKE AND DATA PASS\n")) {
    return 204;
  }
  if (!RunHandshake(server.get(), root.get(),
                    {"other.test", true, valid_time,
                     X509_V_ERR_HOSTNAME_MISMATCH}) ||
      !RunHandshake(server.get(), root.get(),
                    {"gtos.test", false, valid_time,
                     X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY}) ||
      !RunHandshake(server.get(), root.get(),
                    {"gtos.test", true, expired_time,
                     X509_V_ERR_CERT_HAS_EXPIRED}) ||
      !Say("GTOS TLS13 TRUST NAME AND EXPIRY REFUSAL PASS\n")) {
    return 205;
  }
  return 37;
}
