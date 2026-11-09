// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <storage/content_volume_layout.h>

#include "FLAC/metadata.h"
#include "FLAC/stream_decoder.h"
#include "FLAC/stream_encoder.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/synchronization/mutex.h"
#include "fcp/base/platform.h"
#include "iamf/common/read_bit_buffer.h"
#include "iamf/common/write_bit_buffer.h"
#include "mkvmuxer/mkvmuxerutil.h"
#include "tools/gtos_native_sdk/file-input.hpp"
#include "tools/gtos_native_sdk/file-output.hpp"
#include "v8/src/utils/output-stream.h"

namespace {

int TestAbslThreadStorage() {
    using Allocator = absl::base_internal::LowLevelAlloc;
    constexpr size_t kCommitBoundary = 256 * 4096;
    auto* arena = Allocator::NewArena(0);
    auto* bytes = static_cast<unsigned char*>(
        Allocator::AllocWithArena(kCommitBoundary + 1, arena));
    if (!bytes || Allocator::DeleteArena(arena)) return 76;
    bytes[0] = 0x2a;
    bytes[kCommitBoundary] = 0x5b;
    absl::Mutex mutex;
    absl::CondVar ready;
    bool done = false;
    bool valid = false;
    mutex.Lock();
    std::thread worker([&] {
        auto* other = Allocator::AllocWithArena(65537, arena);
        const bool disjoint = other && other != bytes &&
            bytes[0] == 0x2a && bytes[kCommitBoundary] == 0x5b;
        if (other) Allocator::Free(other);
        mutex.Lock();
        valid = disjoint;
        done = true;
        ready.Signal();
        mutex.Unlock();
    });
    while (!done) ready.Wait(&mutex);
    mutex.Unlock();
    worker.join();
    Allocator::Free(bytes);
    return valid && Allocator::DeleteArena(arena) ? 0 : 77;
}

struct FlacResult {
    size_t samples = 0;
    bool error = false;
};

int32_t SampleAt(size_t index) {
    return static_cast<int32_t>(index % 31) * 53 - 701;
}

FLAC__StreamDecoderWriteStatus ReadFlac(const FLAC__StreamDecoder*,
                                      const FLAC__Frame* frame,
                                      const FLAC__int32* const data[], void* context) {
    auto& result = *static_cast<FlacResult*>(context);
    if (frame->header.channels != 1 || frame->header.bits_per_sample != 16)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    for (unsigned i = 0; i < frame->header.blocksize; ++i) {
        if (data[0][i] != SampleAt(result.samples++))
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void FlacError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* context) {
    static_cast<FlacResult*>(context)->error = true;
}

int TestFlacFile() {
    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-flac-probe.flac";
    std::array<FLAC__int32, 4096> input;
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = SampleAt(i);
    std::unique_ptr<FLAC__StreamEncoder, decltype(&FLAC__stream_encoder_delete)>
        encoder(FLAC__stream_encoder_new(), FLAC__stream_encoder_delete);
    if (!encoder || !FLAC__stream_encoder_set_channels(encoder.get(), 1) ||
        !FLAC__stream_encoder_set_bits_per_sample(encoder.get(), 16) ||
        !FLAC__stream_encoder_set_sample_rate(encoder.get(), 8000) ||
        !FLAC__stream_encoder_set_compression_level(encoder.get(), 0) ||
        FLAC__stream_encoder_init_file(encoder.get(), path, nullptr, nullptr) !=
            FLAC__STREAM_ENCODER_INIT_STATUS_OK ||
        !FLAC__stream_encoder_process_interleaved(encoder.get(), input.data(), input.size()) ||
        !FLAC__stream_encoder_finish(encoder.get()))
        return 72;
    FLAC__StreamMetadata metadata{};
    if (!FLAC__metadata_get_streaminfo(path, &metadata) ||
        metadata.data.stream_info.total_samples != input.size() ||
        metadata.data.stream_info.sample_rate != 8000 ||
        metadata.data.stream_info.channels != 1)
        return 73;
    FlacResult result;
    std::unique_ptr<FLAC__StreamDecoder, decltype(&FLAC__stream_decoder_delete)>
        decoder(FLAC__stream_decoder_new(), FLAC__stream_decoder_delete);
    if (!decoder ||
        FLAC__stream_decoder_init_file(decoder.get(), path, ReadFlac, nullptr, FlacError, &result) !=
            FLAC__STREAM_DECODER_INIT_STATUS_OK ||
        !FLAC__stream_decoder_process_until_end_of_stream(decoder.get()) ||
        result.error || result.samples != input.size() ||
        !FLAC__stream_decoder_finish(decoder.get()))
        return 74;
    return 0;
}

int TestFileInput(const char* path, const char* missing) {
    std::string first(8193, 'x');
    first[4095] = '\0';
    first[4096] = static_cast<char>(0xff);
    const std::string bytes = first + "\n\nlast";
    if (!fcp::WriteStringToFile(path, bytes).ok()) return 110;
    {
        gtos::native::FileInput input(path);
        std::string line;
        if (!input.is_open() || !std::getline(input, line) || line != first ||
            !std::getline(input, line) || !line.empty() ||
            !std::getline(input, line) || line != "last" ||
            std::getline(input, line) || !input.eof() || input.bad())
            return 111;
    }
    {
        gtos::native::FileInput input(path);
        const std::string copy((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        if (input.bad() || copy != bytes) return 112;
    }
    gtos::native::FileInput absent(missing);
    std::string line;
    if (absent.is_open() || !absent.fail() || std::getline(absent, line))
        return 113;
    return 0;
}

}  // namespace

int TestFileConsumers() {
    if (const int status = TestAbslThreadStorage()) return status;
    constexpr char path[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-fcp-probe";
    constexpr char missing[] = GTOS_CONTENT_ASSET_DIRECTORY "/gtos-missing/probe";
    std::string bytes(4096, '\x5a');
    bytes[2047] = '\0';
    bytes[4095] = '\n';
    if (!fcp::WriteStringToFile(path, bytes).ok() || !fcp::FileExists(path))
        return 63;
    auto exact = fcp::ReadFileToString(path);
    if (!exact.ok() || *exact != bytes) return 64;

    absl::Cord cord(bytes);
    constexpr std::string_view tail("\0\r\n", 3);
    cord.Append(tail);
    bytes.append(tail);
    if (!fcp::WriteCordToFile(path, cord).ok()) return 65;
    auto copied = fcp::ReadFileToCord(path);
    if (!copied.ok() || *copied != cord || static_cast<std::string>(*copied) != bytes)
        return 66;

    if (!fcp::WriteStringToFile(path, {}).ok()) return 67;
    auto empty = fcp::ReadFileToString(path);
    if (!empty.ok() || !empty->empty() || fcp::FileExists(missing) ||
        fcp::ReadFileToString(missing).ok() || fcp::WriteStringToFile(missing, bytes).ok() ||
        fcp::ReadFileToString(GTOS_CONTENT_ASSET_DIRECTORY).ok())
        return 68;

    {
        gtos::native::FileOutput trace(path);
        trace << "first";
        trace.close();
        if (!trace) return 98;
    }
    {
        gtos::native::FileOutput invalid(path, std::ios_base::in);
        if (invalid) return 99;
        gtos::native::FileOutput trace(path, std::ios_base::app);
        trace.write(tail.data(), tail.size()).flush();
        trace.close();
        if (!trace) return 100;
    }
    const auto appended = fcp::ReadFileToString(path);
    if (!appended.ok() || *appended != std::string_view("first\0\r\n", 8))
        return 101;
    {
        gtos::native::FileOutput trace(path, std::ios_base::trunc);
        trace << "next";
        trace.close();
        if (!trace) return 102;
        gtos::native::FileOutput denied(missing, std::ios_base::app);
        if (denied) return 103;
    }
    const auto truncated = fcp::ReadFileToString(path);
    if (!truncated.ok() || *truncated != "next") return 104;
    {
        v8::internal::FileOutputStream snapshot(path);
        char chunk[] = {'v', '8', '\0', 'x'};
        if (!snapshot.IsOpen() ||
            snapshot.WriteAsciiChunk(chunk, sizeof(chunk)) != v8::OutputStream::kContinue)
            return 107;
        snapshot.EndOfStream();
        if (snapshot.IsOpen()) return 108;
    }
    const auto snapshot = fcp::ReadFileToString(path);
    if (!snapshot.ok() || *snapshot != std::string_view("v8\0x", 4)) return 109;
    if (const int status = TestFileInput(path, missing)) return status;

    auto stream = iamf_tools::StreamBasedReadBitBuffer::Create(2);
    constexpr std::array<uint8_t, 1> first{0xab};
    constexpr std::array<uint8_t, 1> second{0xcd};
    uint16_t value = 0;
    if (!stream || !stream->PushBytes(first).ok() ||
        !absl::IsResourceExhausted(stream->ReadUnsignedLiteral(12, value)) ||
        !stream->PushBytes(second).ok() ||
        !stream->ReadUnsignedLiteral(12, value).ok() || value != 0xabc ||
        !stream->ReadUnsignedLiteral(4, value).ok() || value != 0xd)
        return 69;
    stream->Flush();
    if (!stream->PushBytes(second).ok() ||
        !stream->ReadUnsignedLiteral(8, value).ok() || value != 0xcd)
        return 70;

    iamf_tools::WriteBitBuffer bits(2);
    if (!bits.WriteUnsignedLiteral(0xabc, 12).ok() ||
        !bits.WriteUnsignedLiteral(0xd, 4).ok() || bits.bit_offset() != 16 ||
        bits.bit_buffer().size() != 2 || bits.bit_buffer()[0] != 0xab ||
        bits.bit_buffer()[1] != 0xcd)
        return 71;

    if (const int status = TestFlacFile()) return status;
    unsigned seed = 1, copy = 1, other = 2;
    const auto uid = mkvmuxer::MakeUID(&seed);
    if (!uid || uid >> 56 || seed == 1 || mkvmuxer::MakeUID(&copy) != uid ||
        copy != seed || mkvmuxer::MakeUID(&other) == uid || mkvmuxer::MakeUID(&seed) == uid)
        return 75;
    return 0;
}
