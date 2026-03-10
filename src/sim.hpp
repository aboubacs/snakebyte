#pragma once

#include <vector>
#include <set>
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>
#include <string>

struct SimPos {
    int x, y;
    bool operator==(const SimPos& o) const { return x == o.x && y == o.y; }
    bool operator!=(const SimPos& o) const { return !(*this == o); }
    bool operator<(const SimPos& o) const { return x < o.x || (x == o.x && y < o.y); }
};

enum SimDir { SIM_UP, SIM_DOWN, SIM_LEFT, SIM_RIGHT, SIM_DIR_COUNT };

inline SimPos sim_dir_delta(SimDir d) {
    switch (d) {
        case SIM_UP:    return {0, -1};
        case SIM_DOWN:  return {0, 1};
        case SIM_LEFT:  return {-1, 0};
        case SIM_RIGHT: return {1, 0};
        default:        return {0, 0};
    }
}

inline SimDir sim_opposite(SimDir d) {
    switch (d) {
        case SIM_UP:    return SIM_DOWN;
        case SIM_DOWN:  return SIM_UP;
        case SIM_LEFT:  return SIM_RIGHT;
        case SIM_RIGHT: return SIM_LEFT;
        default:        return d;
    }
}

// Pick a random direction that isn't the opposite of current
inline SimDir sim_random_dir_no_reverse(SimDir current) {
    SimDir opp = sim_opposite(current);
    SimDir d;
    do {
        d = static_cast<SimDir>(rand() % SIM_DIR_COUNT);
    } while (d == opp);
    return d;
}

struct SimSnake {
    int id;
    int owner;
    std::vector<SimPos> body;
    SimDir dir = SIM_UP;
    bool alive = true;

    SimPos head() const { return body[0]; }
    int length() const { return (int)body.size(); }
};

struct SimState {
    int width = 0;
    int height = 0;
    int max_turns = 200;
    int turn = 0;
    bool game_over = false;
    int winner = -1;

    std::vector<std::string> grid;
    std::vector<SimPos> energy;
    std::vector<SimSnake> snakes;

    bool in_bounds(SimPos p) const {
        return p.x >= 0 && p.x < width && p.y >= 0 && p.y < height;
    }

    bool is_platform(SimPos p) const {
        if (!in_bounds(p)) return false;
        return grid[p.y][p.x] == '#';
    }

    bool has_energy(SimPos p) const {
        for (auto& e : energy) if (e == p) return true;
        return false;
    }

    bool is_snake_body(SimPos p, int exclude_id = -1) const {
        for (auto& s : snakes) {
            if (!s.alive || s.id == exclude_id) continue;
            for (auto& bp : s.body)
                if (bp == p) return true;
        }
        return false;
    }

    SimSnake* get_snake(int id) {
        for (auto& s : snakes)
            if (s.id == id) return &s;
        return nullptr;
    }

    const SimSnake* get_snake(int id) const {
        for (auto& s : snakes)
            if (s.id == id) return &s;
        return nullptr;
    }

    std::vector<int> get_alive_ids(int player) const {
        std::vector<int> ids;
        for (auto& s : snakes)
            if (s.owner == player && s.alive) ids.push_back(s.id);
        return ids;
    }

    int count_body_parts(int player) const {
        int total = 0;
        for (auto& s : snakes)
            if (s.owner == player && s.alive)
                total += s.length();
        return total;
    }

    // Set direction for a snake
    void set_dir(int snake_id, SimDir d) {
        SimSnake* s = get_snake(snake_id);
        if (s && s->alive) s->dir = d;
    }

    // Execute one full game step (directions must be set beforehand)
    void step();

    // Evaluate: positive = good for player, negative = bad
    int eval(int player) const {
        return count_body_parts(player) - count_body_parts(1 - player);
    }

private:
    void do_moves();
    void do_eats();
    void do_beheadings();
    void apply_gravity();
    void check_game_over();
};
