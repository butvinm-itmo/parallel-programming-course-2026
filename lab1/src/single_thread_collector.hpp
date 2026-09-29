#pragma once

#include "metrics.hpp"

#include <array>
#include <cstdint>
#include <limits>

class SingleThreadCollector : public MetricsCollector {
    std::array<uint64_t, 256> buckets_{};
    uint64_t count_ = 0;
    uint64_t sum_ = 0;
    uint64_t min_ = std::numeric_limits<uint64_t>::max();
    uint64_t max_ = std::numeric_limits<uint64_t>::min();

  public:
    void record(uint64_t value) override;

    Snapshot snapshot() override;
};
