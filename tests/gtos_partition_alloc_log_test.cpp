#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "partition_alloc/build_config.h"
#include "partition_alloc/partition_alloc_base/check.h"
#include "partition_alloc/partition_alloc_base/logging.h"
#include "partition_alloc/partition_alloc_base/scoped_clear_last_error.h"

namespace logging = partition_alloc::internal::logging;
namespace base = partition_alloc::internal::base;

static_assert(PA_BUILDFLAG(IS_GTOS) && !PA_BUILDFLAG(IS_LINUX) &&
              !PA_BUILDFLAG(IS_POSIX));

namespace {

std::string output;
unsigned calls;
unsigned fail_at;
int fail_result;
bool echo;

void Reset() {
  output.clear();
  calls = 0;
  fail_at = 0;
}

}  // namespace

extern "C" int GtosPartitionAllocLogTestWrite(const char* data, unsigned size) {
  assert(size > 0 && size <= 256);
  if (++calls == fail_at) {
    return fail_result;
  }
  output.append(data, size);
  if (echo) {
    assert(std::fwrite(data, 1, size, stdout) == size);
    assert(std::fflush(stdout) == 0);
  }
  return static_cast<int>(size);
}

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "fatal") == 0) {
    echo = true;
    errno = 19;
    PA_BASE_PCHECK(false) << "fatal consumer";
    return 77;
  }
  assert(argc == 1);

  errno = 19;
  assert(logging::GetLastSystemErrorCode() == 19);
  {
    base::ScopedClearLastError outer;
    assert(errno == 0);
    errno = 42;
    {
      base::ScopedClearLastError inner;
      assert(errno == 0);
      errno = 8;
    }
    assert(errno == 42);
  }
  assert(errno == 19);

  PA_PLOG(ERROR) << "probe";
  assert(calls == 1 && output.find("probe: errno 19\n") != std::string::npos);
  assert(errno == 19);

  for (unsigned length : {255, 256, 257, 519}) {
    Reset();
    const std::string message(length - 1, 'a');
    const std::string terminated = message + '\n';
    logging::RawLog(logging::LOGGING_ERROR, terminated.c_str());
    assert(output == terminated && calls == (length + 255) / 256);
  }

  Reset();
  logging::RawLog(logging::LOGGING_ERROR, "line without newline");
  assert(output == "line without newline\n" && calls == 2);
  Reset();
  logging::SetMinLogLevel(logging::LOGGING_ERROR);
  logging::RawLog(logging::LOGGING_INFO, "filtered");
  logging::RawLog(logging::LOGGING_ERROR, nullptr);
  logging::RawLog(logging::LOGGING_ERROR, "");
  assert(calls == 0 && output.empty());

  const std::string message = std::string(518, 'b') + '\n';
  for (int status : std::array<int, 4>{-14, 0, 1, 257}) {
    Reset();
    fail_at = 2;
    fail_result = status;
    logging::RawLog(logging::LOGGING_ERROR, message.c_str());
    assert(calls == 2 && output == message.substr(0, 256));
  }
  assert(errno == 19);
  std::puts("PARTITION_ALLOC_LOG_TEST_PASS");
}
