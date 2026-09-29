#pragma once

#include "metrics.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

// a cache line on Apple M1 is 128 bytes
struct alignas(128) ThreadState {
    std::array<std::atomic<uint64_t>, bucketsCount> buckets{};
    std::atomic<uint64_t> count = 0;
    std::atomic<uint64_t> sum = 0;
    std::atomic<uint64_t> min = std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> max = std::numeric_limits<uint64_t>::min();
};

class ThreadLocalCollector : public MetricsCollector {
    const uint64_t id_;
    std::mutex listMt_;
    std::vector<std::unique_ptr<ThreadState>> states_;

    ThreadState *get_my_state();

  public:
    ThreadLocalCollector();

    void record(uint64_t value) override;

    Snapshot snapshot() override;
};
