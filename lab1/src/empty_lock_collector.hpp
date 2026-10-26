#pragma once

#include "metrics.hpp"

#include <cstdint>
#include <mutex>

class EmptyLockCollector : public MetricsCollector {
    std::mutex mt_;

  public:
    void record(uint64_t value) override;

    Snapshot snapshot() override;
};
