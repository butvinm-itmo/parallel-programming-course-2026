#pragma once

#include "metrics.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

constexpr std::size_t shardsCount = 16;

class StripedCollector : public MetricsCollector {
    std::array<std::mutex, shardsCount> mts_; // on macOS a mutex is 64 bytes and a cache line is 128 bytes, so we have a false sharing 
    std::array<uint64_t, bucketsCount> buckets_{};
    std::atomic<uint64_t> count_ = 0;
    std::atomic<uint64_t> sum_ = 0;
    std::atomic<uint64_t> min_ = std::numeric_limits<uint64_t>::max();
    std::atomic<uint64_t> max_ = std::numeric_limits<uint64_t>::min();

  public:
    void record(uint64_t value) override;

    Snapshot snapshot() override;
};
