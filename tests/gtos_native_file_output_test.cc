#include "file-output.hpp"

#include <csignal>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/resource.h>

namespace {

unsigned checks;
unsigned failures;

void Check(bool condition, const char* description) {
  ++checks;
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", description);
  }
}

std::string Read(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  using gtos::native::FileOutput;

  const std::string expected("2a:\0ABC", 7);
  FileOutput output("bytes");
  Check(output.good(), "open output file");
  output << std::hex << 42 << ':';
  output.put('\0');
  output.write("ABC", 3);
  output.flush();
  Check(output.good() && Read("bytes") == expected, "format and flush exact bytes");
  output.close();
  Check(output.good(), "close without error");
  output.put('!');
  Check(output.fail(), "write after close fails");
  Check(Read("bytes") == expected, "closed file remains unchanged");

  {
    FileOutput scoped("scoped");
    scoped << "closed by destructor";
    Check(scoped.good(), "buffer scoped output");
  }
  Check(Read("scoped") == "closed by destructor", "destructor flushes and closes");

  FileOutput missing("missing/file");
  Check(missing.fail(), "open failure propagates");
  missing << "discarded";
  Check(missing.fail(), "failed stream stays failed");

  struct rlimit original {};
  if (getrlimit(RLIMIT_FSIZE, &original) != 0 ||
      std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR)
    return 2;
  struct rlimit limited = original;
  limited.rlim_cur = 1;
  if (setrlimit(RLIMIT_FSIZE, &limited) != 0)
    return 2;
  FileOutput short_write("short");
  Check(short_write.good(), "open file with a limited write budget");
  short_write << std::string(8192, 'x') << std::flush;
  Check(short_write.fail(), "short write propagates");
  short_write.close();
  if (setrlimit(RLIMIT_FSIZE, &original) != 0)
    return 2;
  Check(Read("short") == "x", "write stops at the real file limit");

  std::printf("GTOS_FILE_OUTPUT_HOST checks=%u failures=%u\n", checks, failures);
  return failures != 0;
}
