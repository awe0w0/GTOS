// Exercises Chromium's large-blob wire data and integrity on GTOS.
#include <array>
#include <cstdint>
#include <vector>

#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "components/cbor/values.h"
#include "crypto/aead.h"
#include "device/fido/large_blob.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bool CheckArray() {
  constexpr std::array<uint8_t, 17> expected = {
      0x80, 0x76, 0xbe, 0x8b, 0x52, 0x8d, 0x00, 0x75, 0xf7,
      0xaa, 0xe9, 0x8d, 0x6f, 0xa5, 0x7a, 0x6d, 0x3c};
  device::LargeBlobArrayWriter writer({});
  device::LargeBlobArrayReader reader;
  std::vector<uint8_t> bytes;
  while (writer.has_remaining_fragments()) {
    const auto fragment = writer.Pop(5);
    if (fragment.bytes.empty() || fragment.offset != bytes.size()) {
      return false;
    }
    reader.Append(fragment.bytes);
    bytes.insert(bytes.end(), fragment.bytes.begin(), fragment.bytes.end());
  }
  const auto values = reader.Materialize();
  if (base::span(bytes) != base::span(expected) || !values ||
      !values->empty() ||
      device::VerifyLargeBlobArrayIntegrity(base::span(expected).first<16>())) {
    return false;
  }
  bytes.back() ^= 1;
  device::LargeBlobArrayReader rejected;
  rejected.Append(bytes);
  return !device::VerifyLargeBlobArrayIntegrity(bytes) &&
         !rejected.Materialize();
}

bool CheckAssociatedData() {
  device::LargeBlobKey key{};
  constexpr uint64_t original_size = 0x06050403;
  const device::LargeBlob plaintext({0x11, 0x22, 0x33}, original_size);
  const device::LargeBlobData sealed(key, plaintext);
  const auto document = sealed.AsCBOR();
  const auto& map = document.GetMap();
  const auto nonce =
      map.find(cbor::Value(static_cast<int>(device::LargeBlobDataKeys::kNonce)));
  const auto ciphertext = map.find(
      cbor::Value(static_cast<int>(device::LargeBlobDataKeys::kCiphertext)));
  if (nonce == map.end() || ciphertext == map.end()) {
    return false;
  }
  constexpr std::array<uint8_t, 12> expected_aad = {
      'b', 'l', 'o', 'b', 3, 4, 5, 6, 0, 0, 0, 0};
  const auto expected = crypto::aead::Seal(
      crypto::aead::AES_256_GCM, key, plaintext.compressed_data,
      nonce->second.GetBytestring(), expected_aad);
  const auto parsed = device::LargeBlobData::Parse(document);
  if (expected.empty() || expected != ciphertext->second.GetBytestring() ||
      !parsed || parsed->Decrypt(key) != plaintext) {
    return false;
  }
  key.front() = 1;
  return !parsed->Decrypt(key);
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  if (!CheckArray() || !Say("GTOS FIDO LARGE BLOB CBOR AND HASH PASS\n")) {
    return 201;
  }
  if (!CheckAssociatedData() || !Say("GTOS FIDO LARGE BLOB AAD PASS\n")) {
    return 202;
  }
  return 37;
}
