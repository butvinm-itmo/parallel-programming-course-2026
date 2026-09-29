#include "metrics.hpp"

#include <array>
#include <cstdint>

uint64_t percentile(double p, uint64_t count, const std::array<uint64_t, 256> &buckets) {
    double threshold = count * p;
    double acc = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        acc += buckets[i];
        if (acc >= threshold) {
            return i * 4;
        }
    }
    return 255 * 4;
}
