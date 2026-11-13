// Drives Chromium's actual WebM parser and VP9 decoder in an offline GTOS guest.
#include <cstdint>
#include <memory>
#include <optional>

#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/memory_mapped_file.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/single_thread_task_executor.h"
#include "media/base/media_log.h"
#include "media/base/media_tracks.h"
#include "media/base/stream_parser_buffer.h"
#include "media/base/video_frame.h"
#include "media/filters/vpx_video_decoder.h"
#include "media/formats/webm/webm_stream_parser.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

class TestMediaLog : public media::MediaLog {
 public:
  TestMediaLog() = default;
};

class WebmConsumer {
 public:
  WebmConsumer()
      : decoder_(media::OffloadableVideoDecoder::OffloadState::kOffloaded) {}

  bool Run(base::span<const uint8_t> bytes) {
    TestMediaLog log;
    media::WebMStreamParser parser;
    parser.Init(base::BindOnce(&WebmConsumer::OnInit, base::Unretained(this)),
                base::BindRepeating(&WebmConsumer::OnConfig,
                                    base::Unretained(this)),
                base::BindRepeating(&WebmConsumer::OnBuffers,
                                    base::Unretained(this)),
                base::DoNothing(),
                base::BindRepeating(&WebmConsumer::OnSegment,
                                    base::Unretained(this)),
                base::DoNothing(), &log);
    if (!parser.AppendToParseBuffer(bytes)) {
      return false;
    }
    int steps = 0;
    media::StreamParser::ParseStatus progress;
    do {
      progress = parser.Parse(313);
      if (progress == media::StreamParser::ParseStatus::kFailed ||
          ++steps > 300) {
        return false;
      }
    } while (progress ==
             media::StreamParser::ParseStatus::kSuccessHasMoreData);
    if (!DecoderFinish() || invalid_output_ || !saw_init_ || !saw_config_ ||
        segments_ == 0 || packets_ < 8 || frames_ < 8 ||
        first_sample_ == last_sample_) {
      return false;
    }
    return Say("GTOS CHROMIUM WEBM VP9 DEMUX AND DECODE PASS\n");
  }

 private:
  void OnInit(const media::StreamParser::InitParameters& params) {
    saw_init_ = params.detected_video_track_count == 1 &&
                params.detected_audio_track_count == 0;
  }

  bool OnConfig(std::unique_ptr<media::MediaTracks> tracks) {
    if (!tracks || saw_config_ || tracks->GetVideoConfigs().size() != 1 ||
        !tracks->GetAudioConfigs().empty()) {
      return false;
    }
    const auto& [track, config] = *tracks->GetVideoConfigs().begin();
    if (config.codec() != media::VideoCodec::kVP9 ||
        config.is_encrypted() || !config.IsValidConfig()) {
      return false;
    }
    track_ = track;
    visible_ = config.visible_rect().size();
    std::optional<bool> status;
    decoder_.Initialize(config, false, nullptr,
                        base::BindOnce(&WebmConsumer::StoreStatus, &status),
                        base::BindRepeating(&WebmConsumer::OnFrame,
                                            base::Unretained(this)),
                        base::NullCallback());
    saw_config_ = status.value_or(false);
    return saw_config_ && Say("GTOS CHROMIUM WEBM VP9 TRACK PASS\n");
  }

  bool OnBuffers(const media::StreamParser::BufferQueueMap& batches) {
    if (!saw_config_) {
      return false;
    }
    for (const auto& [track, queue] : batches) {
      if (track != track_) {
        return false;
      }
      for (const auto& packet : queue) {
        if (packet->end_of_stream() ||
            (packets_ == 0 && !packet->is_key_frame())) {
          return false;
        }
        std::optional<bool> status;
        decoder_.Decode(packet,
                        base::BindOnce(&WebmConsumer::StoreStatus, &status));
        if (!status.value_or(false)) {
          return false;
        }
        ++packets_;
      }
    }
    return true;
  }

  static void StoreStatus(std::optional<bool>* result,
                          media::DecoderStatus status) {
    *result = status.is_ok();
  }

  void OnSegment() { ++segments_; }

  void OnFrame(scoped_refptr<media::VideoFrame> frame) {
    if (!frame || !frame->HasDirectCpuAccess() ||
        frame->visible_rect().size() != visible_ ||
        (frames_ != 0 && frame->timestamp() <= last_timestamp_)) {
      invalid_output_ = true;
      return;
    }
    constexpr size_t kY = media::VideoFrame::Plane::kY;
    const auto y = frame->GetVisiblePlaneData(kY);
    const size_t row_bytes = frame->GetVisibleRowBytes(kY);
    const size_t rows = frame->GetVisibleRows(kY);
    const size_t stride = frame->stride(kY);
    if (row_bytes < 20 || rows < 20 || stride < row_bytes ||
        (rows - 1) * stride + row_bytes > y.size()) {
      invalid_output_ = true;
      return;
    }
    uint32_t sample = 2166136261u;
    for (size_t row = 0; row < rows; row += 7) {
      for (const uint8_t value : y.subspan(row * stride, row_bytes)) {
        sample = (sample ^ value) * 16777619u;
      }
    }
    if (frames_ == 0) {
      first_sample_ = sample;
      Say("GTOS CHROMIUM WEBM VP9 FIRST PIXELS PASS\n");
    }
    last_sample_ = sample;
    last_timestamp_ = frame->timestamp();
    ++frames_;
  }

  bool DecoderFinish() {
    if (!saw_config_) {
      return false;
    }
    std::optional<bool> status;
    decoder_.Decode(media::DecoderBuffer::CreateEOSBuffer(),
                    base::BindOnce(&WebmConsumer::StoreStatus, &status));
    return status.value_or(false);
  }

  media::VpxVideoDecoder decoder_;
  gfx::Size visible_;
  base::TimeDelta last_timestamp_;
  int track_ = -1;
  int packets_ = 0;
  int frames_ = 0;
  int segments_ = 0;
  uint32_t first_sample_ = 0;
  uint32_t last_sample_ = 0;
  bool saw_init_ = false;
  bool saw_config_ = false;
  bool invalid_output_ = false;
};

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  if (!base::CommandLine::Init(argc, argv)) {
    return 250;
  }
  base::SingleThreadTaskExecutor executor;
  base::MemoryMappedFile input;
  if (!input.Initialize(base::FilePath("/content/bear-vp9.webm")) ||
      input.length() != 67504) {
    return 251;
  }
  WebmConsumer consumer;
  return consumer.Run(input.bytes()) ? 37 : 252;
}
