#include <iostream>
#include <thread>

int data = 0;
bool ready = false;

int main() {
    std::jthread b([] {
        while (!ready) {
        }
        std::cout << data << '\n';
    });
    std::jthread a([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        data = 42;
        ready = true;
    });
}
