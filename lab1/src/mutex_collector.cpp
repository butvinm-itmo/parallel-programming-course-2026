#include "mutex_collector.hpp"

#include "metrics.hpp"
#include "single_thread_collector.hpp"

#include <cstdint>
#include <mutex>

void MutexCollector::record(uint64_t value) {
    std::lock_guard lock(mt_);
    base_.record(value);
}

Snapshot MutexCollector::snapshot() {
    std::lock_guard lock(mt_);
    return base_.snapshot();
}
