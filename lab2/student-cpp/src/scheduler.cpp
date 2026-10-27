// Этап 1–2: реализация Scheduler, point() и await().
// Сейчас все методы бросают исключение «TODO»: заполните их по заданию этапов 1 и 2.
// Деструктор должен дождаться (join) всех запущенных потоков, в том числе после deadlock.
#include "scheduler.hpp"

#include "sched_mutex.hpp"

namespace sched {

thread_local Scheduler *Scheduler::current = nullptr;
thread_local int Scheduler::current_id = -1;

Scheduler::Scheduler() {}

Scheduler::~Scheduler() {}

void Scheduler::spawn(std::function<void()> body) {
    auto t = std::make_unique<Thread>();
    t->body = std::move(body);
    threads.push_back(std::move(t));
}

void Scheduler::run(Strategy &strategy) {
    for (int i = 0; i < (int)threads.size(); i++) {
        threads[i]->th = std::thread([this, i] {
            current = this;
            current_id = i;
            try {
                threads[i]->body();
            } catch (const Aborted &) {
                return;
            }
            threads[i]->done = true;
            sem.release();
        });
        sem.acquire();
    }

    while (true) {
        bool any_alive = false;

        std::vector<int> enabled;
        for (int i = 0; i < (int)threads.size(); i++) {
            const auto &t = *threads[i];
            if (t.done) {
                continue;
            }

            any_alive = true;
            if (!t.ready || t.ready()) {
                enabled.push_back(i);
            }
        }
        if (!any_alive) {
            break;
        }

        if (!enabled.empty()) {
            int i = strategy.pick(enabled, last);
            last = i;
            trace += char('A' + i);
            threads[i]->sem.release();
            sem.acquire();
        } else {
            // deadlock
            std::string held;
            for (auto *m : mutexes) {
                if (m->owner == -1) {
                    continue;
                }
                if (!held.empty()) {
                    held += ", ";
                }
                held += m->name + ":" + char('A' + m->owner);
            }
            deadlock = "held={" + held + "}";

            aborted = true;
            for (const auto &t : threads) {
                if (!t->done) {
                    t->sem.release();
                }
            }
            break;
        }
    }

    for (auto &t : threads) {
        t->th.join();
    }
}

std::string Scheduler::trace_string() const {
    return trace;
}

void Scheduler::add_log(std::string text) {
    if (logging) {
        const auto label = std::string(1, char('A' + Scheduler::current_id));
        log.push_back(label + "  " + text);
    }
}

void Scheduler::pause(std::function<bool()> ready) {
    Thread &t = *threads[current_id];
    t.ready = std::move(ready);
    sem.release();
    t.sem.acquire();
    t.ready = nullptr;
    if (aborted) {
        throw Aborted{};
    }
}

void point() {
    auto s = Scheduler::current;
    s->add_log("window");
}

void await(std::function<bool()> cond) {
    auto *s = Scheduler::current;
    s->pause(cond);
    s->add_log("await done");
}

} // namespace sched
