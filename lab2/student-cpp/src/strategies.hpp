#pragma once
// Стратегии выбора следующего потока.
// FirstAlive — этап 1, Replay — этап 2, RandomPick — этап 3, Dfs — этап 4 (ограничение bound — этап 5).
// enabled — номера потоков, которые могут сделать шаг, по возрастанию; last — прошлый выбранный (-1 в начале).
#include "strategy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sched {

// Всегда первый поток из enabled.
struct FirstAlive : Strategy {
    int pick(const std::vector<int> &enabled, int) override {
        return enabled[0];
    }
};

// Проигрывает трассу: на i-м решении выбирает поток trace[i] ('A' -> 0, 'B' -> 1).
struct Replay : Strategy {
    std::string trace;
    size_t pos = 0;

    explicit Replay(std::string trace) : trace(std::move(trace)) {}

    // Сбросить позицию в трассе перед прогоном.
    void begin_run() override {
        pos = 0;
    }

    int pick(const std::vector<int> &enabled, int) override {
        if (pos >= trace.size()) {
            throw std::runtime_error("tried to pick position " + std::to_string(pos) + " in the trace of size " + std::to_string(trace.size()));
        }
        int i = trace[pos++] - 'A';

        auto it = std::find(enabled.begin(), enabled.end(), i);
        if (it == enabled.end()) {
            throw std::runtime_error("picked thread " + std::string(1, char('A' + i)) + " is not enabled");
        }
        return i;
    }
};

// Случайный поток: enabled[rnd() % enabled.size()], rnd — std::mt19937(seed).
struct RandomPick : Strategy {
    std::mt19937 rnd;

    explicit RandomPick(uint32_t seed) : rnd(seed) {}

    int pick(const std::vector<int> &enabled, int) override {
        return enabled[rnd() % enabled.size()];
    }
};

// Stateless DFS по дереву выборов: каждый прогон с нуля, повторяет префикс прошлого и берёт следующий вариант.
// bound — максимум вытеснений (этап 5); для dfs runner передаёт INT_MAX.
struct Dfs : Strategy {
    struct Node {
        std::vector<int> options;
        size_t chosen = 0;
    };

    // Путь прошлого прогона, по узлу на каждое решение. Живёт между прогонами.
    std::vector<Node> path;
    size_t pos = 0;
    int bound;
    int preemptions = 0;

    explicit Dfs(int bound) : bound(bound) {}

    // Подготовиться к новому прогону (позиция в пути, счётчик вытеснений).
    void begin_run() override {
        pos = 0;
        preemptions = 0;
    }

    int pick(const std::vector<int> &enabled, int last) override {
        bool last_enabled = std::find(enabled.begin(), enabled.end(), last) != enabled.end();

        // Новая развилка: до этого решения ещё не доходили.
        if (pos == path.size()) {
            Node node;
            if (last_enabled && preemptions >= bound) {
                node.options = {last};
            } else {
                node.options = enabled;
            }
            path.push_back(node);
        }

        const Node &node = path[pos];
        int i = node.options[node.chosen];
        pos++;

        // Вытеснение: прошлый поток мог продолжить, но выбран другой.
        if (last_enabled && i != last) {
            preemptions++;
        }
        return i;
    }

    // Выбрать следующий путь; false — дерево обойдено.
    bool end_run() override {
        while (!path.empty() && path.back().chosen + 1 == path.back().options.size()) {
            path.pop_back();
        }
        if (path.empty()) {
            return false;
        }
        path.back().chosen++;
        return true;
    }
};

} // namespace sched
