#include "double_buffer_collector.hpp"
#include "empty_lock_collector.hpp"
#include "metrics.hpp"
#include "mutex_collector.hpp"
#include "single_thread_collector.hpp"
#include "striped_collector.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    std::size_t mid = v.size() / 2;
    if (v.size() % 2 == 1) {
        return v[mid];
    }
    return (v[mid - 1] + v[mid]) / 2.0;
}

std::vector<uint64_t> generate_values(uint32_t seed, uint64_t count) {
    constexpr uint64_t maxValue = 1023;
    constexpr double kExponent = 1.15;

    std::vector<double> weights(maxValue);
    for (uint64_t k = 1; k <= maxValue; ++k) {
        weights[k - 1] = 1.0 / std::pow(k, kExponent);
    }

    std::mt19937 rng{seed};
    std::discrete_distribution<uint64_t> dist(weights.begin(), weights.end());

    std::vector<uint64_t> values(count);
    for (auto &v : values) {
        v = dist(rng) + 1;
    }
    return values;
}

// returns the number of record() calls made by the k-th thread
uint64_t write_loop(MetricsCollector &collector, const std::vector<uint64_t> &values, uint64_t k, std::latch &start, const std::atomic<bool> &stop) {
    uint64_t localOps = 0;
    uint64_t i = k * 1000; // each thread starts from a distinct values range otherwise they all will fall into the same bucket initially and create contention

    start.wait();
    while (!stop.load(std::memory_order_relaxed)) {
        collector.record(values[i]);
        localOps++;
        i++;
        if (i == values.size()) {
            i = 0;
        }
    }
    return localOps;
}

double run(MetricsCollector &collector, const std::vector<uint64_t> &values, uint64_t numThreads, std::chrono::seconds duration) {
    std::latch start{1};
    std::atomic<bool> stop{false};
    std::vector<uint64_t> ops(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (uint64_t k = 0; k < numThreads; ++k) {
        threads.emplace_back([&, k] { // copy k otherwise all threads will have k = numThreads-1
            ops[k] = write_loop(collector, values, k, start, stop);
        });
    }

    std::chrono::time_point t0 = std::chrono::steady_clock::now();
    {
        start.count_down();
        std::this_thread::sleep_for(duration);
        stop.store(true);
    }
    std::chrono::time_point t1 = std::chrono::steady_clock::now();
    double d = std::chrono::duration<double>(t1 - t0).count();

    uint64_t opsTotal = 0;
    for (uint64_t k = 0; k < numThreads; ++k) {
        threads[k].join();
        opsTotal += ops[k];
    }
    return opsTotal / d;
}

double measurePoint(MetricsCollector &collector, const std::vector<uint64_t> &values, uint64_t numThreads) {
    constexpr std::chrono::seconds duration{5};
    constexpr uint64_t repeats = 5;

    run(collector, values, numThreads, duration);

    std::vector<double> results(repeats);
    for (uint64_t i = 0; i < repeats; ++i) {
        results[i] = run(collector, values, numThreads, duration);
    }
    return median(results);
}

struct StressResult {
    uint64_t snapshots;
    uint64_t less;    // sum(buckets) < count
    uint64_t greater; // sum(buckets) > count
    uint64_t ops;     // record() calls made by all writers
    uint64_t count;   // final count after join
    uint64_t broken;  // less + greater
};

StressResult stress(MetricsCollector &collector, const std::vector<uint64_t> &values, uint64_t numWriters, uint64_t numSnapshots) {
    std::latch start{1};
    std::atomic<bool> stop{false};
    std::vector<uint64_t> ops(numWriters);
    std::vector<std::thread> threads;
    threads.reserve(numWriters);

    for (uint64_t k = 0; k < numWriters; ++k) {
        threads.emplace_back([&, k] {
            ops[k] = write_loop(collector, values, k, start, stop);
        });
    }

    StressResult result{.snapshots = numSnapshots};
    start.count_down();
    for (uint64_t s = 0; s < numSnapshots; ++s) {
        Snapshot snap = collector.snapshot();
        uint64_t bucketsSum = 0;
        for (uint64_t b : snap.buckets) {
            bucketsSum += b;
        }
        if (bucketsSum < snap.count) {
            result.less++;
        } else if (bucketsSum > snap.count) {
            result.greater++;
        }
    }
    stop.store(true);

    for (uint64_t k = 0; k < numWriters; ++k) {
        threads[k].join();
        result.ops += ops[k];
    }
    result.broken = result.less + result.greater;
    result.count = collector.snapshot().count;
    return result;
}

enum class Impl {
    SingleThread,
    EmptyLock,
    Mutex,
    Striped,
    ThreadLocal,
    DoubleBuffer,
};

constexpr std::array<std::pair<Impl, std::string_view>, 6> implNames{{
    {Impl::SingleThread, "single_thread"},
    {Impl::EmptyLock, "empty_lock"},
    {Impl::Mutex, "mutex"},
    {Impl::Striped, "striped"},
    {Impl::ThreadLocal, "thread_local"},
    {Impl::DoubleBuffer, "double_buffer"},
}};

std::optional<Impl> parse_impl(std::string_view name) {
    for (const auto &[impl, implName] : implNames) {
        if (implName == name) {
            return impl;
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> parse_threads(std::string_view s) {
    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

std::unique_ptr<MetricsCollector> make_collector(Impl impl) {
    switch (impl) {
    case Impl::SingleThread:
        return std::make_unique<SingleThreadCollector>();
    case Impl::Mutex:
        return std::make_unique<MutexCollector>();
    case Impl::EmptyLock:
        return std::make_unique<EmptyLockCollector>();
    case Impl::Striped:
        return std::make_unique<StripedCollector>();
    case Impl::DoubleBuffer:
        return std::make_unique<DoubleBufferCollector>();
    case Impl::ThreadLocal:
        return nullptr; // not implemented yet
    }
}

void print_usage(const char *program) {
    std::cerr << "usage: " << program << " bench <collector> <maxThreads>" << std::endl;
    std::cerr << "       " << program << " stress <collector> <writers>" << std::endl;
    std::cerr << "collectors:";
    for (const auto &[impl, implName] : implNames) {
        std::cerr << " " << implName;
    }
    std::cerr << std::endl;
}

void run_bench(Impl impl, const std::vector<uint64_t> &values, uint64_t maxThreads) {
    std::cout << "threads,mops,count" << std::endl;
    for (uint64_t numThreads = 1; numThreads <= maxThreads; numThreads <<= 1) {
        std::unique_ptr<MetricsCollector> collector = make_collector(impl);
        double result = measurePoint(*collector, values, numThreads);
        // reading the snapshot keeps the compiler from eliminating record() writes as dead stores
        uint64_t count = collector->snapshot().count;
        std::cout << numThreads << "," << result / 1e6 << "," << count << std::endl;
    }
}

void run_stress(Impl impl, const std::vector<uint64_t> &values, uint64_t numWriters) {
    constexpr uint64_t numSnapshots = 10000;

    std::unique_ptr<MetricsCollector> collector = make_collector(impl);
    StressResult r = stress(*collector, values, numWriters, numSnapshots);
    double brokenPct = 100.0 * r.broken / r.snapshots;
    int64_t diff = static_cast<int64_t>(r.count) - static_cast<int64_t>(r.ops);

    std::cout << "writers,snapshots,broken,broken_pct,less,greater,ops,count,diff" << std::endl;
    std::cout << numWriters << "," << r.snapshots << "," << r.broken << "," << brokenPct << "," << r.less << "," << r.greater << ","
              << r.ops << "," << r.count << "," << diff << std::endl;
}

int main(int argc, char **argv) {
    constexpr uint32_t seed = 34 + 35;
    constexpr uint32_t valuesCount = 1 << 20;

    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    std::string_view mode = argv[1];
    if (mode != "bench" && mode != "stress") {
        std::cerr << "unknown mode: " << mode << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::optional<Impl> impl = parse_impl(argv[2]);
    if (!impl) {
        std::cerr << "unknown collector: " << argv[2] << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::optional<uint64_t> threads = parse_threads(argv[3]);
    if (!threads) {
        std::cerr << "threads must be a positive integer, got: " << argv[3] << std::endl;
        return 1;
    }

    if (!make_collector(*impl)) {
        std::cerr << "collector is not implemented yet: " << argv[2] << std::endl;
        return 1;
    }

    std::vector<uint64_t> values = generate_values(seed, valuesCount);

    if (mode == "bench") {
        run_bench(*impl, values, *threads);
    } else {
        run_stress(*impl, values, *threads);
    }
}
