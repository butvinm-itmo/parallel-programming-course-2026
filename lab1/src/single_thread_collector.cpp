#include "single_thread_collector.hpp"

#include "metrics.hpp"

#include <algorithm>
#include <cstdint>

void SingleThreadCollector::record(uint64_t value) {
    count_++;
    sum_ += value;
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
    uint64_t bucket = std::min<uint64_t>(value / 4, bucketsCount - 1);
    buckets_[bucket]++;
}

Snapshot SingleThreadCollector::snapshot() {
    uint64_t p50 = percentile(0.50, count_, buckets_);
    uint64_t p99 = percentile(0.99, count_, buckets_);
    return Snapshot{
        .buckets = buckets_,
        .count = count_,
        .sum = sum_,
        .min = min_,
        .max = max_,
        .p50 = p50,
        .p99 = p99,
    };
}
