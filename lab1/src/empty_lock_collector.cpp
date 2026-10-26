#include "empty_lock_collector.hpp"

#include "metrics.hpp"

#include <cstdint>
#include <mutex>

void EmptyLockCollector::record(uint64_t) {
    std::lock_guard lock(mt_);
}

Snapshot EmptyLockCollector::snapshot() {
    std::lock_guard lock(mt_);
    return Snapshot{};
}
