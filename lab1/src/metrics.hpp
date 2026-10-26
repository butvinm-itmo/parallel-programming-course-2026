#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

constexpr std::size_t bucketsCount = 256;
constexpr std::size_t bucketWidth = 4;

struct Snapshot {
    std::array<uint64_t, bucketsCount> buckets;
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
    uint64_t p50;
    uint64_t p99;
};

class MetricsCollector {
  public:
    virtual ~MetricsCollector() = default;
    virtual void record(uint64_t value) = 0;
    virtual Snapshot snapshot() = 0;
};

uint64_t percentile(double p, uint64_t count, const std::array<uint64_t, bucketsCount> &buckets);

uint64_t next_collector_id();
