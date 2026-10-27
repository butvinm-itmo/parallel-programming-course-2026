// One thread writes into two collectors in turn.
// TLSSlot is shared by all instances of ThreadLocalCollector, so every record() creates a new ThreadState.
//
// g++ -O2 -std=c++20 -pthread -Isrc repro/tls_slot_bug.cpp $(ls src/*.cpp | grep -v main.cpp) -o tls_slot

#include "thread_local_collector.hpp"

#include <cstdlib>
#include <iostream>
#include <new>

// ThreadState is alignas(128), so it is the only thing allocated through the aligned operator new
static int statesCreated = 0;

void *operator new(std::size_t size, std::align_val_t align) {
    ++statesCreated;
    return std::aligned_alloc(static_cast<std::size_t>(align), size);
}

void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }

int main() {
    ThreadLocalCollector a;
    ThreadLocalCollector b;

    for (int i = 0; i < 5; ++i) {
        a.record(1);
        b.record(1);
    }

    // expected 2: one state per collector
    std::cout << "states created: " << statesCreated << "\n";
}
