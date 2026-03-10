// Auto-generated merged file — do not edit directly
#pragma GCC optimize("-O3")
#pragma GCC optimize("inline")
#pragma GCC optimize("omit-frame-pointer")
#pragma GCC optimize("unroll-loops")

// ===== sim.hpp =====

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

// ===== sim.cpp =====

void SimState::step() {
    turn++;
    do_moves();
    do_eats();
    do_beheadings();
    apply_gravity();
    check_game_over();
}

void SimState::do_moves() {
    for (auto& s : snakes) {
        if (!s.alive) continue;
        SimPos delta = sim_dir_delta(s.dir);
        SimPos new_head = {s.head().x + delta.x, s.head().y + delta.y};
        s.body.insert(s.body.begin(), new_head);
        s.body.pop_back();
    }
}

void SimState::do_eats() {
    std::set<int> eaten_indices;

    for (auto& s : snakes) {
        if (!s.alive) continue;
        SimPos head = s.head();

        for (int i = 0; i < (int)energy.size(); i++) {
            if (energy[i] == head && !eaten_indices.count(i)) {
                s.body.push_back(s.body.back());
                eaten_indices.insert(i);
                break;
            }
        }
    }

    std::vector<int> to_remove(eaten_indices.begin(), eaten_indices.end());
    std::sort(to_remove.rbegin(), to_remove.rend());
    for (int i : to_remove) {
        energy.erase(energy.begin() + i);
    }
}

void SimState::do_beheadings() {
    std::set<int> to_behead;

    for (auto& s : snakes) {
        if (!s.alive) continue;
        SimPos head = s.head();

        if (!in_bounds(head) || is_platform(head)) {
            to_behead.insert(s.id);
            continue;
        }

        bool collides = false;
        for (auto& other : snakes) {
            if (!other.alive) continue;
            int start_idx = (other.id == s.id) ? 1 : 0;
            for (int i = start_idx; i < (int)other.body.size(); i++) {
                if (other.body[i] == head) {
                    collides = true;
                    break;
                }
            }
            if (collides) break;
        }

        if (collides) {
            to_behead.insert(s.id);
        }
    }

    for (int id : to_behead) {
        SimSnake* s = get_snake(id);
        if (!s) continue;
        if (s->length() >= 3) {
            s->body.erase(s->body.begin());
        } else {
            s->alive = false;
        }
    }
}

void SimState::apply_gravity() {
    bool changed = true;
    while (changed) {
        changed = false;

        // Union-find for intercoiled groups
        std::map<int, int> parent;
        for (auto& s : snakes) {
            if (!s.alive) continue;
            parent[s.id] = s.id;
        }

        std::function<int(int)> uf_find = [&](int x) -> int {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        };

        auto unite = [&](int a, int b) {
            a = uf_find(a); b = uf_find(b);
            if (a != b) parent[a] = b;
        };

        for (int i = 0; i < (int)snakes.size(); i++) {
            if (!snakes[i].alive) continue;
            for (int j = i + 1; j < (int)snakes.size(); j++) {
                if (!snakes[j].alive) continue;
                bool adjacent = false;
                for (auto& bp1 : snakes[i].body) {
                    for (auto& bp2 : snakes[j].body) {
                        int dist = abs(bp1.x - bp2.x) + abs(bp1.y - bp2.y);
                        if (dist <= 1) {
                            adjacent = true;
                            break;
                        }
                    }
                    if (adjacent) break;
                }
                if (adjacent) unite(snakes[i].id, snakes[j].id);
            }
        }

        // Collect groups
        std::map<int, std::vector<int>> groups;
        for (int i = 0; i < (int)snakes.size(); i++) {
            if (!snakes[i].alive) continue;
            groups[uf_find(snakes[i].id)].push_back(i);
        }

        // Check support for each group
        for (auto& [gid, indices] : groups) {
            bool supported = false;
            for (int idx : indices) {
                if (supported) break;
                for (auto& bp : snakes[idx].body) {
                    SimPos below = {bp.x, bp.y + 1};
                    if (is_platform(below)) { supported = true; break; }
                    if (has_energy(below)) { supported = true; break; }
                    // Supported by snake from different group
                    for (auto& other : snakes) {
                        if (!other.alive || uf_find(other.id) == gid) continue;
                        for (auto& obp : other.body) {
                            if (obp == below) { supported = true; break; }
                        }
                        if (supported) break;
                    }
                    if (supported) break;
                }
            }

            if (!supported) {
                for (int idx : indices) {
                    for (auto& bp : snakes[idx].body) {
                        bp.y += 1;
                    }
                }
                changed = true;
            }
        }

        // Kill snakes that fell off
        for (auto& s : snakes) {
            if (!s.alive) continue;
            for (auto& bp : s.body) {
                if (!in_bounds(bp)) {
                    s.alive = false;
                    changed = true;
                    break;
                }
            }
        }
    }
}

void SimState::check_game_over() {
    bool p0_alive = !get_alive_ids(0).empty();
    bool p1_alive = !get_alive_ids(1).empty();

    if (!p0_alive || !p1_alive) game_over = true;
    if (energy.empty()) game_over = true;
    if (turn >= max_turns) game_over = true;

    if (game_over) {
        int score0 = count_body_parts(0);
        int score1 = count_body_parts(1);
        if (score0 > score1) winner = 0;
        else if (score1 > score0) winner = 1;
        else winner = -1;
    }
}

// ===== bot.hpp =====

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>


class Bot {
public:
    void init();
    void read_turn();
    void think();

    int depth = 10;  // configurable rollout depth

private:
    int my_id_ = 0;
    int turn_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> grid_;

    std::vector<int> my_snake_ids_;
    std::vector<int> opp_snake_ids_;

    // Current game state rebuilt each turn
    SimState state_;

    // Best move sequence from previous turn (shifted by 1)
    // Outer: turn index in sequence, Inner: one SimDir per alive snake
    std::vector<std::vector<SimDir>> prev_best_seq_;

    // Build a SimState from current parsed data
    SimState build_state() const;

    // Generate a random move sequence of given length for our snakes
    std::vector<std::vector<SimDir>> random_sequence(int len, int num_snakes,
                                                      const std::vector<int>& alive_ids) const;

    // Simulate a sequence and return eval score
    int simulate(const SimState& base, const std::vector<int>& alive_ids,
                 const std::vector<std::vector<SimDir>>& seq) const;
};

// ===== bot.cpp =====

static SimDir parse_body_direction(const SimPos& head, const SimPos& neck) {
    int dx = head.x - neck.x;
    int dy = head.y - neck.y;
    if (dy < 0) return SIM_UP;
    if (dy > 0) return SIM_DOWN;
    if (dx < 0) return SIM_LEFT;
    if (dx > 0) return SIM_RIGHT;
    return SIM_UP;
}

void Bot::init() {
    std::cin >> my_id_ >> width_ >> height_;
    std::cin.ignore();

    grid_.resize(height_);
    for (int y = 0; y < height_; y++) {
        std::getline(std::cin, grid_[y]);
    }

    int snakes_per_player;
    std::cin >> snakes_per_player;

    my_snake_ids_.resize(snakes_per_player);
    opp_snake_ids_.resize(snakes_per_player);

    for (int i = 0; i < snakes_per_player; i++) std::cin >> my_snake_ids_[i];
    for (int i = 0; i < snakes_per_player; i++) std::cin >> opp_snake_ids_[i];
}

void Bot::read_turn() {
    turn_++;

    state_.width = width_;
    state_.height = height_;
    state_.grid = grid_;
    state_.turn = turn_ - 1;  // step() will increment
    state_.game_over = false;
    state_.winner = -1;
    state_.energy.clear();
    state_.snakes.clear();

    int energy_count;
    std::cin >> energy_count;
    for (int i = 0; i < energy_count; i++) {
        int x, y;
        std::cin >> x >> y;
        state_.energy.push_back({x, y});
    }

    int snake_count;
    std::cin >> snake_count;
    std::cin.ignore();

    for (int i = 0; i < snake_count; i++) {
        std::string line;
        std::getline(std::cin, line);

        std::istringstream iss(line);
        int id;
        std::string body_str;
        iss >> id >> body_str;

        SimSnake snake;
        snake.id = id;
        snake.alive = true;

        // Determine owner
        bool is_mine = false;
        for (int mid : my_snake_ids_) {
            if (mid == id) { is_mine = true; break; }
        }
        snake.owner = is_mine ? my_id_ : (1 - my_id_);

        // Parse body: "x1,y1:x2,y2:..."
        std::istringstream bs(body_str);
        std::string segment;
        while (std::getline(bs, segment, ':')) {
            int cx, cy;
            char comma;
            std::istringstream ss(segment);
            ss >> cx >> comma >> cy;
            snake.body.push_back({cx, cy});
        }

        // Infer direction from head and neck
        if (snake.body.size() >= 2) {
            snake.dir = parse_body_direction(snake.body[0], snake.body[1]);
        }

        state_.snakes.push_back(snake);
    }
}

std::vector<std::vector<SimDir>> Bot::random_sequence(int len, int num_snakes,
                                                      const std::vector<int>& alive_ids) const {
    // Track current direction per snake to avoid backwards moves
    std::vector<SimDir> cur_dirs(num_snakes);
    for (int s = 0; s < num_snakes; s++) {
        const SimSnake* sn = nullptr;
        for (auto& ss : state_.snakes)
            if (ss.id == alive_ids[s]) { sn = &ss; break; }
        cur_dirs[s] = sn ? sn->dir : SIM_UP;
    }

    std::vector<std::vector<SimDir>> seq(len);
    for (int t = 0; t < len; t++) {
        seq[t].resize(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            SimDir d = sim_random_dir_no_reverse(cur_dirs[s]);
            seq[t][s] = d;
            cur_dirs[s] = d;
        }
    }
    return seq;
}

int Bot::simulate(const SimState& base, const std::vector<int>& alive_ids,
                  const std::vector<std::vector<SimDir>>& seq) const {
    SimState sim = base;
    int steps = (int)seq.size();

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) break;

        // Apply our moves
        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s < (int)seq[t].size()) {
                sim.set_dir(alive_ids[s], seq[t][s]);
            }
        }

        // Opponent doesn't move (keeps current direction)
        sim.step();
    }

    return sim.eval(my_id_);
}

void Bot::think() {
    auto alive_ids = state_.get_alive_ids(my_id_);

    if (alive_ids.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    int num_snakes = (int)alive_ids.size();
    int rollout_depth = depth;

    // Time budget: 40ms per turn (referee enforces 50ms)
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::milliseconds(40);

    int best_eval = -999999;
    std::vector<std::vector<SimDir>> best_seq;

    // Seed with previous best sequence shifted by 1
    if (!prev_best_seq_.empty()) {
        std::vector<std::vector<SimDir>> shifted;
        for (int t = 1; t < (int)prev_best_seq_.size(); t++) {
            shifted.push_back(prev_best_seq_[t]);
        }
        // Pad with random moves to fill depth, respecting no-reverse
        // Track direction from end of shifted sequence (or from current state)
        std::vector<SimDir> pad_dirs(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            if (!shifted.empty()) {
                pad_dirs[s] = shifted.back()[s];
            } else {
                const SimSnake* sn = state_.get_snake(alive_ids[s]);
                pad_dirs[s] = sn ? sn->dir : SIM_UP;
            }
        }
        while ((int)shifted.size() < rollout_depth) {
            std::vector<SimDir> moves(num_snakes);
            for (int s = 0; s < num_snakes; s++) {
                SimDir d = sim_random_dir_no_reverse(pad_dirs[s]);
                moves[s] = d;
                pad_dirs[s] = d;
            }
            shifted.push_back(moves);
        }
        shifted.resize(rollout_depth);

        int eval = simulate(state_, alive_ids, shifted);
        if (eval > best_eval) {
            best_eval = eval;
            best_seq = shifted;
        }
    }

    // Monte Carlo: generate random sequences and keep the best
    int iterations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto seq = random_sequence(rollout_depth, num_snakes, alive_ids);
        int eval = simulate(state_, alive_ids, seq);

        if (eval > best_eval) {
            best_eval = eval;
            best_seq = seq;
        }
        iterations++;
    }

    // Store best sequence for next turn
    prev_best_seq_ = best_seq;

    // Output first moves of best sequence
    if (best_seq.empty()) {
        // Fallback: random (no reverse)
        const char* dirs[] = {"UP", "DOWN", "LEFT", "RIGHT"};
        std::string out;
        for (int s = 0; s < num_snakes; s++) {
            if (!out.empty()) out += ";";
            const SimSnake* sn = state_.get_snake(alive_ids[s]);
            SimDir cur = sn ? sn->dir : SIM_UP;
            SimDir d = sim_random_dir_no_reverse(cur);
            out += std::to_string(alive_ids[s]) + " " + dirs[d];
        }
        std::cout << out << std::endl;
        return;
    }

    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int s = 0; s < num_snakes; s++) {
        if (!out.empty()) out += ";";
        SimDir d = best_seq[0][s];
        out += std::to_string(alive_ids[s]) + " " + dir_names[d]
             + " mc_d" + std::to_string(rollout_depth)
             + "_i" + std::to_string(iterations)
             + "_e" + std::to_string(best_eval);
    }
    std::cout << out << std::endl;
}

// ===== main.cpp =====
#pragma GCC optimize("-O3")
#pragma GCC optimize("inline")
#pragma GCC optimize("omit-frame-pointer")
#pragma GCC optimize("unroll-loops")


#include <cstdlib>
#include <ctime>
#include <unistd.h>

int main() {
    srand(time(nullptr) ^ getpid());

    Bot bot;

#ifdef BOT_DEPTH
    bot.depth = BOT_DEPTH;
#endif

    bot.init();

    while (true) {
        bot.read_turn();
        bot.think();
    }

    return 0;
}
