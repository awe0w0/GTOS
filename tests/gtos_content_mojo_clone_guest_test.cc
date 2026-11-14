#include <memory>
#include <utility>
#include <vector>

#include <process/abi.h>
#include "mojo/public/cpp/bindings/clone_traits.h"

namespace {

struct MoveOnly {
    explicit MoveOnly(int value) : data(std::make_unique<int>(value)) {}
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;

    MoveOnly Clone() const { return MoveOnly(*data); }

    std::unique_ptr<int> data;
};

}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
    std::vector<bool> input(71);
    for (size_t i = 0; i < input.size(); ++i) input[i] = i % 7 == 0;
    auto copied = mojo::Clone(std::as_const(input));
    if (copied != input || !mojo::Clone(std::vector<bool>()).empty()) return 201;
    input[0] = false;
    input[64] = true;
    if (!copied[0] || copied[64]) return 202;

    std::vector<MoveOnly> source;
    source.emplace_back(9);
    source.emplace_back(17);
    auto deep = mojo::Clone(std::as_const(source));
    if (deep.size() != source.size() || *deep[0].data != 9 || *deep[1].data != 17 ||
        deep[0].data == source[0].data || deep[1].data == source[1].data) return 203;
    *source[0].data = 1;
    if (*deep[0].data != 9) return 204;

    const char passed[] = "GTOS MOJO CLONE BOOL AND MOVE-ONLY PASS\n";
    unsigned call = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(call) : "b"(passed), "c"(sizeof(passed) - 1)
                 : "cc", "memory");
    return 37;
}
