// Exercise the real Dawn GTOS branches against host libc file/environment calls.
// This tests adapter semantics, not GTOS syscalls or a working GPU backend.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <utility>

#include "src/dawn/common/DynamicLib.h"
#include "src/dawn/common/SystemHandle.h"
#include "src/dawn/common/SystemUtils.h"

static_assert(DAWN_PLATFORM_IS(GTOS));
static_assert(!DAWN_PLATFORM_IS(POSIX) && !DAWN_PLATFORM_IS(LINUX));

namespace {
unsigned checks = 0;
unsigned failures = 0;
void Check(bool value, const char* text, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, text);
    }
}
#define CHECK(value) Check((value), #value, __LINE__)

void DynamicLibraries() {
    dawn::DynamicLib library;
    std::string error;
    CHECK(!library.Valid());
    CHECK(!library.Open("libMissing.so", &error));
    CHECK(error.find("GTOS dynamic libraries are unavailable") != std::string::npos);
    CHECK(!library.Valid());
    error.clear();
    CHECK(!library.OpenLoaded("libMissing.so", &error));
    CHECK(!error.empty());
    CHECK(!library.Open("libMissing.so"));
    CHECK(!library.OpenLoaded("libMissing.so"));
    error.clear();
    CHECK(library.GetProc("MissingSymbol", &error) == nullptr);
    CHECK(error.find("MissingSymbol") != std::string::npos);
    void (*function)() = reinterpret_cast<void (*)()>(1);
    CHECK(!library.GetProc(&function, "MissingSymbol"));
    CHECK(function == nullptr);
    const std::string paths[] = {"/one/", "/two/"};
    CHECK(!library.Open("libMissing.so", paths, &error));
    dawn::DynamicLib moved(std::move(library));
    library = std::move(moved);
    library.Close();
    library.Close();
    CHECK(!library.Valid() && !moved.Valid());
}

void Environment() {
    constexpr auto name = "GTOS_DAWN_TEST_ENVIRONMENT";
    CHECK(dawn::SetEnvironmentVar(name, nullptr));
    CHECK(!dawn::GetEnvironmentVar(name).second);
    CHECK(dawn::SetEnvironmentVar(name, ""));
    CHECK(dawn::GetEnvironmentVar(name) == std::make_pair(std::string(), true));
    CHECK(dawn::SetEnvironmentVar(name, "original"));
    {
        dawn::ScopedEnvironmentVar outer(name, "temporary");
        CHECK(dawn::GetEnvironmentVar(name).first == "temporary");
        {
            dawn::ScopedEnvironmentVar inner(name, nullptr);
            CHECK(!dawn::GetEnvironmentVar(name).second);
        }
        CHECK(dawn::GetEnvironmentVar(name).first == "temporary");
    }
    CHECK(dawn::GetEnvironmentVar(name).first == "original");
    CHECK(!dawn::SetEnvironmentVar("", "rejected"));
    CHECK(!dawn::SetEnvironmentVar("invalid=name", "rejected"));
    CHECK(dawn::GetEnvironmentVar(name).first == "original");
    CHECK(dawn::SetEnvironmentVar(name, nullptr));
    CHECK(std::strcmp(dawn::GetPathSeparator(), "/") == 0);
    CHECK(!dawn::GetExecutableDirectory().has_value());
    CHECK(!dawn::GetModuleDirectory().has_value());
}

void Handles() {
    char path[] = "gtos-dawn-handles-XXXXXX";
    const int raw = mkstemp(path);
    CHECK(raw >= 0);
    if (raw < 0) return;
    CHECK(unlink(path) == 0);
    int survivor = -1;
    {
        auto first = dawn::SystemHandle::Acquire(raw);
        CHECK(first.IsValid() && first.Get() == raw);
        CHECK(write(first.Get(), "abcd", 4) == 4);
        auto second = first.Duplicate();
        CHECK(second.IsValid() && second.Get() != first.Get());
        CHECK(lseek(second.Get(), 1, SEEK_SET) == 1);
        char byte = 0;
        CHECK(read(first.Get(), &byte, 1) == 1 && byte == 'b');
        CHECK(lseek(second.Get(), 0, SEEK_CUR) == 2);
        dawn::SystemHandle moved(std::move(second));
        CHECK(!second.IsValid() && moved.IsValid());
        const int replaced = moved.Get();
        moved = std::move(first);
        CHECK(!first.IsValid() && moved.Get() == raw);
        errno = 0;
        CHECK(fcntl(replaced, F_GETFD) == -1 && errno == EBADF);
        moved = std::move(moved);
        CHECK(moved.IsValid());
        auto copy = dawn::SystemHandle::Duplicate(raw);
        CHECK(fcntl(raw, F_GETFD) >= 0);
        survivor = copy.Detach();
        CHECK(!copy.IsValid() && fcntl(survivor, F_GETFD) >= 0);
        auto out = dawn::SystemHandle::Acquire(dup(raw));
        const int closed = out.Get();
        CHECK(*out.GetMut() == -1 && !out.IsValid());
        errno = 0;
        CHECK(fcntl(closed, F_GETFD) == -1 && errno == EBADF);
    }
    errno = 0;
    CHECK(fcntl(raw, F_GETFD) == -1 && errno == EBADF);
    CHECK(fcntl(survivor, F_GETFD) >= 0);
    CHECK(close(survivor) == 0);
    dawn::SystemHandle empty;
    CHECK(!empty.IsValid() && empty.Detach() == -1);
}
}  // namespace

int main() {
    DynamicLibraries();
    Environment();
    for (unsigned i = 0; i < 8; ++i) Handles();
    std::printf("DAWN GTOS ADAPTER checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
