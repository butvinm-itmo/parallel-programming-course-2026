#pragma once

#include "metrics.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

constexpr int nowhere = -1;

struct Buf {
    std::array<uint64_t, bucketsCount> buckets{};
    uint64_t count = 0;
    uint64_t sum = 0;
    uint64_t min = std::numeric_limits<uint64_t>::max();
    uint64_t max = std::numeric_limits<uint64_t>::min();

    void clear();
    void merge(const Buf &other);
};

// a cache line on Apple M1 is 128 bytes
struct alignas(128) ThreadBuffers {
    std::atomic<int> inside{nowhere};
    std::array<Buf, 2> bufs;
};

// Recheck = false drops the second active_ check in record(), it is broken on purpose for the stage 4 experiment
template <bool Recheck>
class BasicDoubleBufferCollector : public MetricsCollector {
    const uint64_t id_;
    std::atomic<int> active_{0};
    std::mutex snapMt_;
    std::vector<std::unique_ptr<ThreadBuffers>> threads_;
    Buf global_;

    ThreadBuffers *get_my_buffers();

  public:
    BasicDoubleBufferCollector();

    void record(uint64_t value) override;

    Snapshot snapshot() override;
};

using DoubleBufferCollector = BasicDoubleBufferCollector<true>;
using NoRecheckDoubleBufferCollector = BasicDoubleBufferCollector<false>;
