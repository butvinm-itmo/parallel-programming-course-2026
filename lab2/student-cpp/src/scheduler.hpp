#pragma once
// Этап 1: планировщик, который ведёт настоящие потоки строго по одному (передача хода через семафоры).
// Этап 2: обнаружение deadlock в главном цикле и корректное завершение застрявших потоков.
// Внутреннее устройство (поля, семафоры, вспомогательные функции для shared и sched_mutex) придумываете сами.
#include "strategy.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace sched {

struct Aborted {};

class sched_mutex;

struct Thread {
    std::thread th;
    std::binary_semaphore sem{0};
    std::function<void()> body;
    bool done{false};
    std::function<bool()> ready;
};

class Scheduler {
    std::binary_semaphore sem{0};
    std::vector<std::unique_ptr<Thread>> threads;
    int last = -1;
    std::string trace;

  public:
    Scheduler();
    ~Scheduler();
    // Зарегистрировать поток. Потоки получают буквы A, B, C… в порядке вызова spawn.
    void spawn(std::function<void()> body);
    // Запустить потоки и вести их по одному: на каждой паузе спросить strategy.pick(enabled, last).
    void run(Strategy &strategy);
    // Трасса прогона: буквы потоков, выбранных во всех решениях подряд, например "AAAABB".
    std::string trace_string() const;
    // Добавляет в лог запись с идентификатором текущего потока
    void add_log(std::string text);
    // Отдать ход паузы планировщику, ready - функция ожидания готовности потока (например, для lock)
    void pause(std::function<bool()> ready = nullptr);

    // Планировщик текущего потока
    static thread_local Scheduler *current;
    // id текущего
    static thread_local int current_id;
    // Если true, каждая операция с общими данными добавляет строку в log. Выставляет runner.
    bool logging = false;
    // Лог прогона: строки "<поток>  <операция>", например "A  lock mu", "A  balance -> 100".
    std::vector<std::string> log;
    // Пустая строка, если deadlock не было; иначе "held={a:A, b:B}" (этап 2).
    std::string deadlock;
    // Мьютексы, чтобы перечислить в deadlock
    std::vector<sched_mutex *> mutexes;
    // Потоки попали в deadlock
    bool aborted = false;
};

// window() из тестовых программ: пауза перед следующей операцией с общими данными обязательна.
void point();
// Ждать, пока cond не станет true. До этого поток не попадает в enabled.
void await(std::function<bool()> cond);

} // namespace sched
