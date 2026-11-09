// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <storage/content_volume_layout.h>

#include "fcp/base/platform.h"
#include "tensorflow_lite_support/cc/task/core/external_file_handler.h"
#include "tensorflow_lite_support/cc/task/core/task_utils.h"
#include "tensorflow_lite_support/cc/utils/common_utils.h"

int TestTfliteFileConsumers() {
    using tflite::task::core::ExternalFile;
    using tflite::task::core::ExternalFileHandler;
    using tflite::task::core::LoadBinaryContent;
    using tflite::support::utils::LoadVocabAndIndexFromFile;
    using tflite::support::utils::LoadVocabFromFile;
    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-tflite-file-probe";
    constexpr char missing[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-missing/model";
    constexpr int64_t offset = 17;
    constexpr int64_t length = 4099;
    std::string payload(8211, '\0');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>(i % 251);
    if (!fcp::WriteStringToFile(path, payload).ok() ||
        LoadBinaryContent(path) != payload || !LoadBinaryContent(missing).empty())
        return 114;

    ExternalFile by_name;
    by_name.set_file_name(path);
    {
        const auto handler = ExternalFileHandler::CreateFromExternalFile(&by_name);
        if (!handler.ok() || (*handler)->GetFileContent() != payload) return 115;
    }
    by_name.set_file_name(missing);
    const auto absent = ExternalFileHandler::CreateFromExternalFile(&by_name);
    if (absent.status().code() != absl::StatusCode::kNotFound) return 116;
    by_name.set_file_content(payload);
    const auto memory = ExternalFileHandler::CreateFromExternalFile(&by_name);
    if (!memory.ok() || (*memory)->GetFileContent() != payload) return 117;

    const int fd = open(path, O_RDONLY);
    if (fd < 0 || lseek(fd, 3, SEEK_SET) != 3) return 118;
    ExternalFile by_descriptor;
    auto* range = by_descriptor.mutable_file_descriptor_meta();
    range->set_fd(fd);
    range->set_offset(offset);
    range->set_length(length);
    {
        const auto handler = ExternalFileHandler::CreateFromExternalFile(&by_descriptor);
        if (!handler.ok() ||
            (*handler)->GetFileContent() != std::string_view(payload).substr(offset, length))
            return 119;
    }
    range->set_length(0);
    {
        const auto handler = ExternalFileHandler::CreateFromExternalFile(&by_descriptor);
        if (!handler.ok() ||
            (*handler)->GetFileContent() != std::string_view(payload).substr(offset))
            return 120;
    }
    range->set_offset(-1);
    if (ExternalFileHandler::CreateFromExternalFile(&by_descriptor).status().code() !=
        absl::StatusCode::kInvalidArgument)
        return 121;
    range->set_offset(offset);
    range->set_length(std::numeric_limits<int64_t>::max());
    if (ExternalFileHandler::CreateFromExternalFile(&by_descriptor).status().code() !=
        absl::StatusCode::kInvalidArgument)
        return 122;
    char next = 0;
    if (lseek(fd, 0, SEEK_CUR) != 3 || read(fd, &next, 1) != 1 ||
        next != payload[3] || close(fd) != 0)
        return 123;

    if (!fcp::WriteStringToFile(path, "alpha 1\n\nbeta 7").ok() ||
        LoadVocabFromFile(path) != std::vector<std::string>{"alpha 1", "beta 7"} ||
        !LoadVocabFromFile(missing).empty())
        return 124;
    const auto vocab = LoadVocabAndIndexFromFile(path);
    const auto alpha = vocab.find("alpha");
    const auto beta = vocab.find("beta");
    return vocab.size() == 2 && alpha != vocab.end() && beta != vocab.end() &&
        alpha->second == 1 && beta->second == 7 ? 0 : 125;
}
