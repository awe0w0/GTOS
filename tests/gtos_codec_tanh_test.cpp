#include <cmath>
#include <cstdio>
#include <limits>

extern "C" double gtos_codec_tanh_under_test(double);

int main() {
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double smallest = std::numeric_limits<double>::denorm_min();
    const double cutoff = 0.125;
    const double edges[] = {
        0.0, -0.0, smallest, -smallest, 1e-100, -1e-100,
        std::nextafter(cutoff, 0.0), cutoff, std::nextafter(cutoff, infinity),
        std::nextafter(-cutoff, 0.0), -cutoff, std::nextafter(-cutoff, -infinity),
        19.0, -19.0, 20.0, -20.0, 1e100, -1e100, infinity, -infinity, nan
    };
    unsigned checks = 0;
    unsigned failures = 0;
    double worst_relative = 0.0;
    auto compare = [&](double input) {
        const double expected = std::tanh(input);
        const double actual = gtos_codec_tanh_under_test(input);
        bool matched;
        if (std::isnan(expected)) {
            matched = std::isnan(actual);
        } else if (expected == 0.0) {
            matched = actual == 0.0 && std::signbit(actual) == std::signbit(expected);
        } else {
            const double relative = std::abs(actual / expected - 1.0);
            matched = std::isfinite(actual) && relative <= 8 * std::numeric_limits<double>::epsilon();
            if (relative > worst_relative) worst_relative = relative;
        }
        ++checks;
        if (!matched) {
            if (failures < 12)
                std::printf("FAIL input=%a expected=%a actual=%a\n", input, expected, actual);
            ++failures;
        }
    };
    for (double input : edges) compare(input);
    for (int i = -20000; i <= 20000; ++i) compare(i / 1000.0);
    std::printf("GTOS CODEC TANH checks=%u failures=%u worst_relative=%.17g\n",
                checks, failures, worst_relative);
    return failures ? 1 : 0;
}
