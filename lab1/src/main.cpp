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

double run(MetricsCollector &collector, const std::vector<uint64_t> &values, uint64_t numThreads, std::chrono::seconds duration) {
    std::latch start{1};
    std::atomic<bool> stop{false};
    std::vector<uint64_t> ops(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (uint64_t k = 0; k < numThreads; ++k) {
        threads.emplace_back([&, k] { // copy k otherwise all threads will have k = numThreads-1
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

            ops[k] = localOps;
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
    case Impl::ThreadLocal:
    case Impl::DoubleBuffer:
        return nullptr; // not implemented yet
    }
}

void print_usage(const char *program) {
    std::cerr << "usage: " << program << " <collector> <maxThreads>" << std::endl;
    std::cerr << "collectors:";
    for (const auto &[impl, implName] : implNames) {
        std::cerr << " " << implName;
    }
    std::cerr << std::endl;
}

int main(int argc, char **argv) {
    constexpr uint32_t seed = 34 + 35;
    constexpr uint32_t valuesCount = 1 << 20;

    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::optional<Impl> impl = parse_impl(argv[1]);
    if (!impl) {
        std::cerr << "unknown collector: " << argv[1] << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::optional<uint64_t> maxThreads = parse_threads(argv[2]);
    if (!maxThreads) {
        std::cerr << "maxThreads must be a positive integer, got: " << argv[2] << std::endl;
        return 1;
    }

    if (!make_collector(*impl)) {
        std::cerr << "collector is not implemented yet: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<uint64_t> values = generate_values(seed, valuesCount);

    std::cout << "threads,mops,count" << std::endl;
    for (uint64_t numThreads = 1; numThreads <= *maxThreads; numThreads <<= 1) {
        std::unique_ptr<MetricsCollector> collector = make_collector(*impl);
        double result = measurePoint(*collector, values, numThreads);
        // reading the snapshot keeps the compiler from eliminating record() writes as dead stores
        uint64_t count = collector->snapshot().count;
        std::cout << numThreads << "," << result / 1e6 << "," << count << std::endl;
    }
}
