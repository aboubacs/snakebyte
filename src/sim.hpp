#pragma once

#include <vector>
#include <set>
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

    // Distance heuristic: average of 1/(1+dist) for up to k closest energies per snake.
    // Normalized to [0, 0.5) so it never outweighs eating (+1 from eval).
    double energy_proximity(int player, int k = 3) const {
        auto alive = get_alive_ids(player);
        if (alive.empty() || energy.empty()) return 0.0;

        double total = 0.0;
        int count = 0;
        for (int id : alive) {
            const SimSnake* sn = get_snake(id);
            if (!sn || !sn->alive) continue;
            SimPos head = sn->head();

            std::vector<int> dists;
            dists.reserve(energy.size());
            for (auto& e : energy) {
                dists.push_back(abs(head.x - e.x) + abs(head.y - e.y));
            }
            int take = std::min((int)dists.size(), k);
            std::partial_sort(dists.begin(), dists.begin() + take, dists.end());
            for (int i = 0; i < take; i++) {
                total += 1.0 / (1.0 + dists[i]);
                count++;
            }
        }
        if (count == 0) return 0.0;
        return 0.5 * total / count;
    }

    // Height advantage: average normalized head height across alive snakes.
    // Higher = better (more mobility, further from bottom edge).
    // Returns [0, 0.3]
    double height_advantage(int player) const {
        auto alive = get_alive_ids(player);
        if (alive.empty() || height <= 0) return 0.0;
        double total = 0.0;
        int count = 0;
        for (int id : alive) {
            const SimSnake* sn = get_snake(id);
            if (!sn || !sn->alive) continue;
            total += (double)(height - 1 - sn->head().y) / (height - 1);
            count++;
        }
        if (count == 0) return 0.0;
        return 0.3 * total / count;
    }

    // Territory: count energies closer to me than to any opponent snake.
    // Returns normalized [-0.5, 0.5] (my_share - opp_share).
    double territory(int player) const {
        if (energy.empty()) return 0.0;
        auto my_alive = get_alive_ids(player);
        auto opp_alive = get_alive_ids(1 - player);
        if (my_alive.empty() && opp_alive.empty()) return 0.0;

        int my_count = 0, opp_count = 0;
        for (auto& e : energy) {
            int my_best = 9999;
            for (int id : my_alive) {
                const SimSnake* sn = get_snake(id);
                if (!sn || !sn->alive) continue;
                int d = abs(sn->head().x - e.x) + abs(sn->head().y - e.y);
                if (d < my_best) my_best = d;
            }
            int opp_best = 9999;
            for (int id : opp_alive) {
                const SimSnake* sn = get_snake(id);
                if (!sn || !sn->alive) continue;
                int d = abs(sn->head().x - e.x) + abs(sn->head().y - e.y);
                if (d < opp_best) opp_best = d;
            }
            if (my_best < opp_best) my_count++;
            else if (opp_best < my_best) opp_count++;
        }
        return 0.5 * (my_count - opp_count) / (int)energy.size();
    }

    double center_control(int player) const {
        auto alive = get_alive_ids(player);
        if (alive.empty() || width <= 1) return 0.0;
        double total = 0.0;
        int count = 0;
        double half_w = (width - 1) / 2.0;
        for (int id : alive) {
            const SimSnake* sn = get_snake(id);
            if (!sn || !sn->alive) continue;
            total += 1.0 - std::abs(sn->head().x - half_w) / half_w;
            count++;
        }
        if (count == 0) return 0.0;
        return total / count;
    }

private:
    void do_moves_and_eats();
    void do_beheadings();
    void apply_gravity();
    void check_game_over();
};
