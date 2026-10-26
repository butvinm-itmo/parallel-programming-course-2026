#include "thread_local_collector.hpp"

#include "metrics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>

// more efficient than fetch_add
// the owner is the single writer, so a plain load and store is enough
static void relaxed_add(std::atomic<uint64_t> &c, uint64_t delta) {
    c.store(c.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
}

ThreadLocalCollector::ThreadLocalCollector() : id_(next_collector_id()) {}

ThreadState *ThreadLocalCollector::get_my_state() {
    struct TLSSlot {
        uint64_t id = 0;
        ThreadState *state = nullptr;
    };
    static thread_local TLSSlot slot;

    if (slot.id != id_) {
        auto state = std::make_unique<ThreadState>();
        ThreadState *raw = state.get();
        {
            std::lock_guard lock(listMt_);
            states_.push_back(std::move(state));
        }
        slot.id = id_;
        slot.state = raw;
    }
    return slot.state;
}

void ThreadLocalCollector::record(uint64_t value) {
    ThreadState *s = get_my_state();
    uint64_t bucket = std::min<uint64_t>(value / bucketWidth, bucketsCount - 1);

    relaxed_add(s->buckets[bucket], 1);
    relaxed_add(s->count, 1);
    relaxed_add(s->sum, value);

    if (value < s->min.load(std::memory_order_relaxed)) {
        s->min.store(value, std::memory_order_relaxed);
    }
    if (value > s->max.load(std::memory_order_relaxed)) {
        s->max.store(value, std::memory_order_relaxed);
    }
}

Snapshot ThreadLocalCollector::snapshot() {
    std::array<uint64_t, bucketsCount> buckets{};
    uint64_t count = 0;
    uint64_t sum = 0;
    uint64_t min = std::numeric_limits<uint64_t>::max();
    uint64_t max = std::numeric_limits<uint64_t>::min();

    std::lock_guard lock(listMt_);
    for (const auto &s : states_) {
        for (std::size_t i = 0; i < bucketsCount; ++i) {
            buckets[i] += s->buckets[i].load(std::memory_order_relaxed);
        }
        count += s->count.load(std::memory_order_relaxed);
        sum += s->sum.load(std::memory_order_relaxed);
        min = std::min(min, s->min.load(std::memory_order_relaxed));
        max = std::max(max, s->max.load(std::memory_order_relaxed));
    }

    uint64_t p50 = percentile(0.50, count, buckets);
    uint64_t p99 = percentile(0.99, count, buckets);
    return Snapshot{
        .buckets = buckets,
        .count = count,
        .sum = sum,
        .min = min,
        .max = max,
        .p50 = p50,
        .p99 = p99,
    };
}
