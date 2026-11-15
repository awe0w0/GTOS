// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <openssl/err.h>
#include <process/abi.h>

#include "base/at_exit.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/platform_thread.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/direct_receiver.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/platform/platform_channel_endpoint.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "third_party/boringssl/src/crypto/internal.h"  // nogncheck
#include "tools/gtos_native_sdk/native-thread.h"

namespace {

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

IpczResult UnexpectedTransportActivity(IpczHandle, const void*, size_t,
                                      const IpczDriverHandle*, size_t,
                                      IpczTransportActivityFlags,
                                      const IpczTransportActivityOptions*) {
  __builtin_trap();
}

int CheckTransport() {
  if (!mojo::IsAsyncIOSupported()) {
    return 254;
  }
  if (mojo::IsDirectReceiverSupported()) {
    Say("GTOS MOJO DIRECT RECEIVER UNEXPECTED\n");
    return 253;
  }
  Say("GTOS MOJO DIRECT RECEIVER UNAVAILABLE\n");

  mojo::PlatformChannel channel;
  if (channel.local_endpoint().is_valid() ||
      channel.remote_endpoint().is_valid() ||
      mojo::PlatformChannelEndpoint::RecoverFromString("42").is_valid()) {
    return 241;
  }

  // A valid file handle still cannot be activated as an ipcz transport.
  base::File file(base::FilePath("/content/index.html"),
                  base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return 242;
  }
  mojo::PlatformChannelEndpoint endpoint(
      mojo::PlatformHandle(base::ScopedFD(file.TakePlatformFile())));
  mojo::PlatformChannelEndpoint::HandlePassingInfo passing;
  std::string token("stale");
  endpoint.PrepareToPass(passing, token);
  if (!endpoint.is_valid() || !token.empty()) {
    return 243;
  }

  const IpczDriver& driver = mojo::core::GetIpczDriverForMojo();
  const IpczDriverHandle transport = mojo::core::CreateIpczTransportFromEndpoint(
      std::move(endpoint), {true, false});
  if (transport == IPCZ_INVALID_DRIVER_HANDLE) {
    return 244;
  }
  const IpczResult activation =
      driver.ActivateTransport(transport, IPCZ_INVALID_HANDLE,
                               UnexpectedTransportActivity, 0, nullptr);
  if (driver.Close(transport, 0, nullptr) != IPCZ_RESULT_OK) {
    return 245;
  }
  if (activation != IPCZ_RESULT_UNIMPLEMENTED) {
    return 246;
  }
  return 0;
}

struct CryptoJob {
  raw_ptr<bssl::StaticMutex> lock = nullptr;
  raw_ptr<const std::atomic<bool>> release = nullptr;
  int reason;
  std::atomic<bool> ready{false};
  std::atomic<bool> isolated{false};
};

void* CryptoWorker(void* argument) {
  auto& job = *static_cast<CryptoJob*>(argument);
  const bool initially_empty = ERR_get_error() == 0;
  job.lock->LockRead();
  ERR_put_error(ERR_LIB_USER, 0, job.reason, __FILE__, __LINE__);
  job.ready.store(true, std::memory_order_release);
  while (!job.release->load(std::memory_order_acquire)) {
    base::PlatformThread::YieldCurrentThread();
  }
  const uint32_t error = ERR_get_error();
  job.isolated.store(initially_empty && ERR_GET_LIB(error) == ERR_LIB_USER &&
                         ERR_GET_REASON(error) == job.reason &&
                         ERR_get_error() == 0,
                     std::memory_order_release);
  // Leave an error for the real thread exit cleanup.
  ERR_put_error(ERR_LIB_USER, 0, job.reason, __FILE__, __LINE__);
  job.lock->UnlockRead();
  return &job;
}

bool CheckCryptoThreads() {
  constexpr size_t kWorkers = 5;
  bssl::StaticMutex lock;
  std::atomic<bool> release{false};
  std::array<CryptoJob, kWorkers> jobs{};
  std::array<uint32_t, kWorkers> ids{};
  size_t started = 0;
  for (; started < kWorkers; ++started) {
    auto& job = jobs[started];
    job.lock = &lock;
    job.release = &release;
    job.reason = 1 + static_cast<int>(started);
    if (gtos::native::StartThread(0, CryptoWorker, &job, &ids[started]) != 0) {
      break;
    }
  }

  bool ready = false;
  if (started == kWorkers) {
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
      ready = true;
      for (const auto& job : jobs) {
        ready &= job.ready.load(std::memory_order_acquire);
      }
      if (ready) {
        break;
      }
      base::PlatformThread::YieldCurrentThread();
    }
  }
  // All five workers hold a real reader slot at the same time.
  bool passed = ready && ERR_get_error() == 0;
  release.store(true, std::memory_order_release);
  for (size_t index = 0; index < started; ++index) {
    void* result = nullptr;
    const int joined = gtos::native::JoinThread(ids[index], &result);
    passed &= joined == 0 && result == &jobs[index] &&
              jobs[index].isolated.load(std::memory_order_acquire);
  }
  if (!passed) {
    return false;
  }

  lock.LockWrite();
  lock.UnlockWrite();
  CryptoJob reused{&lock, &release, 7};
  uint32_t id = 0;
  void* result = nullptr;
  return gtos::native::StartThread(0, CryptoWorker, &reused, &id) == 0 &&
         gtos::native::JoinThread(id, &result) == 0 && id > ids.back() &&
         result == &reused && reused.isolated.load(std::memory_order_acquire);
}

int CheckLocalPipe() {
  Say("GTOS MOJO LOCAL BEGIN\n");
  mojo::ScopedMessagePipeHandle sender, receiver;
  if (mojo::CreateMessagePipe(nullptr, &sender, &receiver) != MOJO_RESULT_OK) {
    return 247;
  }
  Say("GTOS MOJO PORTALS OPEN\n");
  std::vector<uint8_t> received;
  if (mojo::ReadMessageRaw(receiver.get(), &received, nullptr,
                           MOJO_READ_MESSAGE_FLAG_NONE) !=
      MOJO_RESULT_SHOULD_WAIT) {
    return 248;
  }
  Say("GTOS MOJO EMPTY PIPE CHECKED\n");
  constexpr std::array<uint8_t, 5> payload{0x47, 0x00, 0x54, 0x4f, 0x53};
  if (mojo::WriteMessageRaw(sender.get(), payload.data(), payload.size(),
                            nullptr, 0, MOJO_WRITE_MESSAGE_FLAG_NONE) !=
          MOJO_RESULT_OK ||
      mojo::ReadMessageRaw(receiver.get(), &received, nullptr,
                           MOJO_READ_MESSAGE_FLAG_NONE) != MOJO_RESULT_OK ||
      received != std::vector<uint8_t>(payload.begin(), payload.end())) {
    return 249;
  }
  Say("GTOS MOJO PAYLOAD CHECKED\n");
  sender.reset();
  if (mojo::ReadMessageRaw(receiver.get(), &received, nullptr,
                           MOJO_READ_MESSAGE_FLAG_NONE) !=
      MOJO_RESULT_FAILED_PRECONDITION) {
    return 250;
  }
  return 0;
}

}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  mojo::core::Configuration config;
  config.is_broker_process = true;
  mojo::core::Init(config);
  const int transport = CheckTransport();
  const int pipe = transport ? 0 : CheckLocalPipe();
  const bool crypto = !transport && !pipe && CheckCryptoThreads();
  Say("GTOS MOJO LOCAL RESULT\n");
  mojo::core::ShutDown();
  Say("GTOS MOJO SHUTDOWN RETURN\n");
  if (transport || pipe) {
    return transport ? transport : pipe;
  }
  if (!crypto || !Say("GTOS CONTENT BORINGSSL THREADS PASS\n")) {
    return 252;
  }
  return Say("GTOS CONTENT MOJO LOCAL PASS\n") ? 37 : 251;
}
