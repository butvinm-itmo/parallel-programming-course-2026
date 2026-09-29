#include "striped_collector.hpp"

#include "metrics.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>

void StripedCollector::record(uint64_t value) {
    uint64_t bucket = std::min<uint64_t>(value / bucketWidth, bucketsCount - 1);
    {
        std::mutex &mt = mts_[bucket % shardsCount];
        std::lock_guard lock(mt);
        buckets_[bucket]++;
    }

    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(value, std::memory_order_relaxed);

    // weak because we are in a retry loop and handle spurious fails anyway
    uint64_t cur = min_.load(std::memory_order_relaxed);
    while (value < cur && !min_.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }

    cur = max_.load(std::memory_order_relaxed);
    while (value > cur && !max_.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }
}

Snapshot StripedCollector::snapshot() {
    std::array<uint64_t, bucketsCount> buckets{};
    for (uint64_t shard = 0; shard < shardsCount; ++shard) {
        std::mutex &mt = mts_[shard];
        std::lock_guard lock(mt);
        for (std::size_t i = shard; i < bucketsCount; i += shardsCount) {
            buckets[i] = buckets_[i];
        }
    }

    // metrics might be out of sync with the buckets
    uint64_t p50 = percentile(0.50, count_, buckets);
    uint64_t p99 = percentile(0.99, count_, buckets);
    return Snapshot{
        .buckets = buckets,
        .count = count_,
        .sum = sum_,
        .min = min_,
        .max = max_,
        .p50 = p50,
        .p99 = p99,
    };
}
