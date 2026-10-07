#include <cstdio>
#include <cstdlib>
extern "C" [[noreturn]] void gtos_skia_assert_failure(void) {
    std::fputs("GTOS PNG HOST SKIA ASSERTION FAILURE\n",stderr);
    std::abort();
}
