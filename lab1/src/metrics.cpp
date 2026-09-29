#include "metrics.hpp"

#include <array>
#include <atomic>
#include <cstdint>

uint64_t percentile(double p, uint64_t count, const std::array<uint64_t, bucketsCount> &buckets) {
    double threshold = count * p;
    double acc = 0;
    for (uint64_t i = 0; i < buckets.size(); ++i) {
        acc += buckets[i];
        if (acc >= threshold) {
            return i * bucketWidth;
        }
    }
    return (bucketsCount - 1) * bucketWidth;
}

uint64_t next_collector_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}
