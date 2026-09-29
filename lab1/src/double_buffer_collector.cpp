#include "double_buffer_collector.hpp"

#include "metrics.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

void Buf::clear() {
    buckets.fill(0);
    count = 0;
    sum = 0;
    min = std::numeric_limits<uint64_t>::max();
    max = std::numeric_limits<uint64_t>::min();
}

void Buf::merge(const Buf &other) {
    for (std::size_t i = 0; i < bucketsCount; ++i) {
        buckets[i] += other.buckets[i];
    }
    count += other.count;
    sum += other.sum;
    min = std::min(min, other.min);
    max = std::max(max, other.max);
}

// thread_local slot outlives collectors, so it has to remember which collector the buffers belong to
static uint64_t next_collector_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

DoubleBufferCollector::DoubleBufferCollector() : id_(next_collector_id()) {}

ThreadBuffers *DoubleBufferCollector::get_my_buffers() {
    struct Slot {
        uint64_t id = 0;
        ThreadBuffers *buffers = nullptr;
    };
    static thread_local Slot slot;

    if (slot.id != id_) {
        auto buffers = std::make_unique<ThreadBuffers>();
        ThreadBuffers *raw = buffers.get();
        {
            std::lock_guard lock(snapMt_);
            threads_.push_back(std::move(buffers));
        }
        slot.id = id_;
        slot.buffers = raw;
    }
    return slot.buffers;
}

void DoubleBufferCollector::record(uint64_t value) {
    ThreadBuffers *my = get_my_buffers();

    int b;
    while (true) {
        b = active_.load();
        my->inside.store(b);
        if (active_.load() == b) { 
            break;
        }
        my->inside.store(nowhere, std::memory_order_release);
    }

    Buf &buf = my->bufs[b];
    uint64_t bucket = std::min<uint64_t>(value / bucketWidth, bucketsCount - 1);
    buf.buckets[bucket]++;
    buf.count++;
    buf.sum += value;
    buf.min = std::min(buf.min, value);
    buf.max = std::max(buf.max, value);

    my->inside.store(nowhere, std::memory_order_release);
}

Snapshot DoubleBufferCollector::snapshot() {
    std::lock_guard lock(snapMt_);

    int old = active_.load();
    active_.store(1 - old);

    for (const auto &t : threads_) {
        while (t->inside.load() == old) {
            std::this_thread::yield();
        }
        Buf &frozen = t->bufs[old];
        global_.merge(frozen);
        frozen.clear();
    }

    uint64_t p50 = percentile(0.50, global_.count, global_.buckets);
    uint64_t p99 = percentile(0.99, global_.count, global_.buckets);
    return Snapshot{
        .buckets = global_.buckets,
        .count = global_.count,
        .sum = global_.sum,
        .min = global_.min,
        .max = global_.max,
        .p50 = p50,
        .p99 = p99,
    };
}
