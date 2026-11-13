// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <stdlib.h>
#include <string>
#include <string_view>

#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/i18n/icu_util.h"
#include "base/path_service.h"
#include "content/shell/grit/shell_resources.h"
#include "third_party/icu/source/common/unicode/unorm2.h"
#include "third_party/zlib/google/compression_utils.h"
#include "ui/base/resource/data_pack.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bool CheckPaths() {
  base::FilePath cwd;
  const base::FilePath root("/");
  const base::FilePath page("/content/index.html");
  if (!base::PathService::Get(base::DIR_CURRENT, &cwd) || cwd != root ||
      base::MakeAbsoluteFilePath(base::FilePath(".")) != root ||
      base::MakeAbsoluteFilePath(base::FilePath("content/./index.html")) != page ||
      base::MakeAbsoluteFilePath(base::FilePath("/content/../content//index.html")) != page ||
      !base::MakeAbsoluteFilePath(base::FilePath()).empty() ||
      !base::MakeAbsoluteFilePath(base::FilePath("/missing/../content/index.html")).empty() ||
      !base::MakeAbsoluteFilePath(base::FilePath("/content/index.html/")).empty() ||
      !base::MakeAbsoluteFilePath(base::FilePath("/content/index.html/..")).empty() ||
      !base::MakeAbsoluteFilePath(base::FilePath("/../content/index.html")).empty()) {
    return false;
  }
  const base::FilePath assets("/content");
  return unsetenv("HOME") == 0 && base::GetHomeDir() == assets &&
         setenv("HOME", "/", 1) == 0 && base::GetHomeDir() == root &&
         setenv("HOME", "relative", 1) == 0 && base::GetHomeDir() == assets &&
         unsetenv("HOME") == 0;
}

bool CheckNormalization() {
  UErrorCode error = U_ZERO_ERROR;
  const auto* normalizer = unorm2_getNFCInstance(&error);
  if (U_FAILURE(error) || !normalizer) {
    return false;
  }

  constexpr std::array<char16_t, 4> source{u'e', u'\u0301', u'A', u'\u030a'};
  constexpr std::array<char16_t, 2> expected{u'\u00e9', u'\u00c5'};
  std::array<char16_t, 8> result{};
  const int32_t size = unorm2_normalize(normalizer, source.data(), source.size(),
                                        result.data(), result.size(), &error);
  return U_SUCCESS(error) && size == static_cast<int32_t>(expected.size()) &&
         std::u16string_view(result.data(), expected.size()) ==
             std::u16string_view(expected.data(), expected.size());
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets) ||
      assets != base::FilePath("/content")) {
    return 231;
  }
  if (!CheckPaths() || !Say("GTOS CHROMIUM PATH SERVICE PASS\n")) {
    return 238;
  }
  {
    using base::File;
    const auto page = assets.AppendASCII("index.html");
    File shared(page, File::FLAG_OPEN | File::FLAG_READ |
                          File::FLAG_WIN_SHARE_DELETE | File::FLAG_WIN_EXCLUSIVE_WRITE);
    File invalid(page, File::FLAG_OPEN | File::FLAG_READ | File::FLAG_WIN_EXECUTE);
    if (!shared.IsValid() || shared.GetLength() <= 0 || invalid.IsValid() ||
        invalid.error_details() != File::FILE_ERROR_INVALID_OPERATION) {
      return 239;
    }
  }
  if (!base::i18n::InitializeICU() || !CheckNormalization() ||
      !Say("GTOS CHROMIUM ICU DATA PASS\n")) {
    return 232;
  }

  ui::DataPack pack(ui::kScaleFactorNone);
  const auto missing = pack.LoadFromPathWithError(assets.AppendASCII("missing.pak"));
  if (missing.has_value() ||
      missing.error().reason != ui::DataPack::FailureReason::kOpenFile) {
    return 233;
  }
  const auto invalid = pack.LoadFromPathWithError(assets.AppendASCII("index.html"));
  if (invalid.has_value() ||
      invalid.error().reason != ui::DataPack::FailureReason::kBadPakVersion) {
    return 234;
  }

  if (!pack.LoadFromPath(assets.AppendASCII("content_shell.pak"))) {
    return 235;
  }
  const auto png = pack.GetStringView(IDR_CONTENT_SHELL_BROKEN_CANVAS);
  const auto html = pack.GetStringView(IDR_CONTENT_SHELL_DEVTOOLS_DISCOVERY_PAGE);
  if (!png || !png->starts_with("\x89PNG\r\n\x1a\n") ||
      pack.GetStringView(0).has_value()) {
    return 236;
  }
  std::string discovery;
  if (!html || !compression::GzipUncompress(base::span(*html), &discovery) ||
      !discovery.contains("<title>Content shell remote debugging</title>")) {
    return 240;
  }
  return Say("GTOS CHROMIUM DATA PACK PASS\n") ? 37 : 237;
}
