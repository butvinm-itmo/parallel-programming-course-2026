#pragma once
// Этап 1: общая переменная. Перед каждым чтением/записью — точка переключения (пауза планировщика),
// после — строка в лог: чтение "<имя> -> <значение>", запись "<имя> = <значение>".
// get_acquire/set_release для планировщика ведут себя так же, как get/set.
#include "scheduler.hpp"

#include <string>

namespace sched {

template <class T>
class shared {
  public:
    shared(std::string name, T value) : name_(std::move(name)), v_(value) {}

    shared(const shared &) = delete;
    shared &operator=(const shared &) = delete;

    // Чтение с паузой перед ним.
    T get() {
        auto s = Scheduler::current;
        s->pause();
        T v = v_;
        if (s->logging) {
            s->add_log(name_ + " -> " + std::to_string(v));
        }
        return v;
    }

    // Запись с паузой перед ней.
    void set(T x) {
        Scheduler *s = Scheduler::current;
        s->pause();
        v_ = x;
        if (s->logging) {
            s->add_log(name_ + " = " + std::to_string(x));
        }
    }

    // То же, что get(): планировщик не моделирует порядки памяти.
    T get_acquire() { return get(); }

    // То же, что set(x).
    void set_release(T x) { set(x); }

    // Чтение без паузы и без лога: для проверок после прогона и условий await.
    T peek() const { return v_; }

    operator T() { return get(); }

    shared &operator=(T x) {
        set(x);
        return *this;
    }

    // Чтение + запись: две точки переключения.
    shared &operator+=(T x) {
        T t = get();
        set(t + x);
        return *this;
    }

    shared &operator-=(T x) {
        T t = get();
        set(t - x);
        return *this;
    }

  private:
    std::string name_;
    T v_;
};

} // namespace sched
