#pragma once
// Этап 2: лок, о котором знает планировщик. Поток, ждущий занятый лок, не попадает в enabled;
// обычный std::mutex здесь нельзя — поток уснёт с ходом в руках и всё зависнет.
// Лог: "lock <имя>", "unlock <имя>". Планировщик должен знать все локи, чтобы печатать held={a:A, b:B}.
#include "scheduler.hpp"

#include <cassert>
#include <string>

namespace sched {

class sched_mutex {
    int owner = -1;

  public:
    // Создаётся в setup() тестовой программы; зарегистрируйте лок в планировщике s.
    sched_mutex(Scheduler &s, std::string name) : name(std::move(name)) { (void)s; }

    sched_mutex(const sched_mutex &) = delete;

    // Пауза; если лок занят — ждать, не участвуя в выборе; затем стать владельцем.
    void lock() {
        Scheduler *s = Scheduler::current;
        s->pause([this] { return owner == -1; });
        owner = Scheduler::current_id;
        s->add_log("lock " + name);
    }

    // Пауза; освободить лок, ждавшие потоки снова могут быть выбраны.
    void unlock() {
        Scheduler *s = Scheduler::current;
        s->pause();
        assert(owner == Scheduler::current_id);
        owner = -1;
        s->add_log("unlock " + name);
    }

    const std::string name;
};

// RAII: lock сейчас, unlock на закрывающей скобке. Выдан готовым.
class sched_guard {
  public:
    explicit sched_guard(sched_mutex &m) : m_(m) { m_.lock(); }

    ~sched_guard() noexcept(false) { m_.unlock(); }

    sched_guard(const sched_guard &) = delete;

  private:
    sched_mutex &m_;
};

} // namespace sched
