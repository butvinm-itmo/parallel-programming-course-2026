#pragma once

#include "metrics.hpp"
#include "single_thread_collector.hpp"

#include <cstdint>
#include <mutex>

class MutexCollector : public MetricsCollector {
    std::mutex mt_;
    SingleThreadCollector base_;

  public:
    void record(uint64_t value) override;

    Snapshot snapshot() override;
};
