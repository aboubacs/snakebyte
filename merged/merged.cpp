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

private:
    void do_moves_and_eats();
    void do_beheadings();
    void apply_gravity();
    void check_game_over();
};

// ===== sim.cpp =====

void SimState::step() {
    turn++;
    do_moves_and_eats();
    do_beheadings();
    apply_gravity();
    check_game_over();
}

void SimState::do_moves_and_eats() {
    std::set<int> eaten_indices;

    for (auto& s : snakes) {
        if (!s.alive) continue;
        SimPos delta = sim_dir_delta(s.dir);
        SimPos new_head = {s.head().x + delta.x, s.head().y + delta.y};
        s.body.insert(s.body.begin(), new_head);

        // Check if head lands on energy — if so, keep tail (grow); otherwise remove it
        bool ate = false;
        for (int i = 0; i < (int)energy.size(); i++) {
            if (energy[i] == new_head && !eaten_indices.count(i)) {
                eaten_indices.insert(i);
                ate = true;
                break;
            }
        }
        if (!ate) {
            s.body.pop_back();
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

// ===== gaof/bot.hpp =====

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cmath>


// An individual is a sequence of moves: [step][snake]
using Individual = std::vector<std::vector<SimDir>>;

struct ScoredIndividual {
    Individual seq;
    double score;
};

class Bot {
public:
    void init();
    void read_turn();
    void think();

    int depth = 10;
    int pop_size = 50;
    double mutation_rate = 0.15;
    bool cumulative_eval = true;
    int energy_k = 3;
    bool eval_decay = false;
    int cheat_factor = 1;
    int flex_count = 5;  // number of random opp sequences for flex re-ranking

    // Opponent simulation parameters
    int opp_depth = 6;
    double opp_time_pct = 0.4;  // 40% of time for opponent simulation

private:
    int my_id_ = 0;
    int turn_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> grid_;

    std::vector<int> my_snake_ids_;
    std::vector<int> opp_snake_ids_;

    SimState state_;

    // Best individual from previous turn
    Individual prev_best_;

    // Predicted opponent moves from last think()
    Individual opp_moves_;

    // Get initial directions for alive snakes
    std::vector<SimDir> get_initial_dirs(const std::vector<int>& alive_ids) const;

    // Generate a random individual respecting no-reverse
    Individual random_individual(int len, int num_snakes,
                                 const std::vector<SimDir>& initial_dirs) const;

    // Repair reverse moves after crossover
    void repair(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    // Crossover operators
    Individual crossover_time_split(const Individual& a, const Individual& b,
                                     const std::vector<SimDir>& initial_dirs) const;
    Individual crossover_snake_split(const Individual& a, const Individual& b,
                                      int num_snakes,
                                      const std::vector<SimDir>& initial_dirs) const;

    // Mutate an individual in place
    void mutate(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    // Evaluate an individual (uses opp_moves_ for opponent simulation)
    double evaluate(const SimState& base, const std::vector<int>& alive_ids,
                    const Individual& ind,
                    const std::vector<int>& opp_alive_ids,
                    const Individual& opp_ind) const;

    // Evaluate for opponent (flipped perspective)
    double evaluate_opp(const SimState& base, const std::vector<int>& opp_alive_ids,
                        const Individual& opp_ind,
                        const std::vector<int>& my_alive_ids) const;

    // Select a parent via fitness-proportional selection
    int select_parent(const std::vector<ScoredIndividual>& pop) const;

    // Run GA for a set of snake IDs with a given deadline and eval function
    std::vector<ScoredIndividual> run_ga(const std::vector<int>& alive_ids,
                      const std::vector<SimDir>& initial_dirs,
                      int ga_depth, Individual* seed,
                      std::chrono::steady_clock::time_point deadline,
                      bool for_opponent,
                      const std::vector<int>& other_alive_ids,
                      const Individual& other_moves,
                      int& generations_out);
};

// ===== gaof/bot.cpp =====

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
    state_.turn = turn_ - 1;
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

        bool is_mine = false;
        for (int mid : my_snake_ids_) {
            if (mid == id) { is_mine = true; break; }
        }
        snake.owner = is_mine ? my_id_ : (1 - my_id_);

        std::istringstream bs(body_str);
        std::string segment;
        while (std::getline(bs, segment, ':')) {
            int cx, cy;
            char comma;
            std::istringstream ss(segment);
            ss >> cx >> comma >> cy;
            snake.body.push_back({cx, cy});
        }

        if (snake.body.size() >= 2) {
            snake.dir = parse_body_direction(snake.body[0], snake.body[1]);
        }

        state_.snakes.push_back(snake);
    }
}

// ===== GA helpers =====

std::vector<SimDir> Bot::get_initial_dirs(const std::vector<int>& alive_ids) const {
    std::vector<SimDir> dirs;
    for (int id : alive_ids) {
        const SimSnake* sn = state_.get_snake(id);
        dirs.push_back(sn ? sn->dir : SIM_UP);
    }
    return dirs;
}

Individual Bot::random_individual(int len, int num_snakes,
                                   const std::vector<SimDir>& initial_dirs) const {
    std::vector<SimDir> cur = initial_dirs;
    Individual ind(len);
    for (int t = 0; t < len; t++) {
        ind[t].resize(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            SimDir d = sim_random_dir_no_reverse(cur[s]);
            ind[t][s] = d;
            cur[s] = d;
        }
    }
    return ind;
}

void Bot::repair(Individual& ind, const std::vector<SimDir>& initial_dirs) const {
    int num_snakes = (int)initial_dirs.size();
    std::vector<SimDir> prev = initial_dirs;
    for (auto& step : ind) {
        for (int s = 0; s < num_snakes; s++) {
            if (step[s] == sim_opposite(prev[s])) {
                step[s] = prev[s];  // keep going same direction rather than reverse
            }
            prev[s] = step[s];
        }
    }
}

Individual Bot::crossover_time_split(const Individual& a, const Individual& b,
                                      const std::vector<SimDir>& initial_dirs) const {
    int len = (int)a.size();
    int split = 1 + rand() % (len - 1);  // [1, len-1]
    Individual child(len);
    for (int t = 0; t < len; t++) {
        child[t] = (t < split) ? a[t] : b[t];
    }
    repair(child, initial_dirs);
    return child;
}

Individual Bot::crossover_snake_split(const Individual& a, const Individual& b,
                                       int num_snakes,
                                       const std::vector<SimDir>& initial_dirs) const {
    // For each snake, randomly pick which parent to take from
    std::vector<bool> use_a(num_snakes);
    for (int s = 0; s < num_snakes; s++) {
        use_a[s] = rand() % 2;
    }

    int len = (int)a.size();
    Individual child(len);
    for (int t = 0; t < len; t++) {
        child[t].resize(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            child[t][s] = use_a[s] ? a[t][s] : b[t][s];
        }
    }
    repair(child, initial_dirs);
    return child;
}

void Bot::mutate(Individual& ind, const std::vector<SimDir>& initial_dirs) const {
    int num_snakes = (int)initial_dirs.size();
    bool mutated = false;

    for (int t = 0; t < (int)ind.size(); t++) {
        for (int s = 0; s < num_snakes; s++) {
            if ((rand() % 1000) < (int)(mutation_rate * 1000)) {
                // Get the previous direction for this snake
                SimDir prev = (t > 0) ? ind[t - 1][s] : initial_dirs[s];
                ind[t][s] = sim_random_dir_no_reverse(prev);
                mutated = true;
            }
        }
    }

    // If we mutated, repair downstream to fix any new reverses
    if (mutated) {
        repair(ind, initial_dirs);
    }
}

double Bot::evaluate(const SimState& base, const std::vector<int>& alive_ids,
                     const Individual& ind,
                     const std::vector<int>& opp_alive_ids,
                     const Individual& opp_ind) const {
    SimState sim = base;
    int steps = (int)ind.size();
    double score = 0.0;
    bool has_opp_moves = !opp_ind.empty();

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            if (cumulative_eval) {
                double final_eval = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_);
                for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            }
            if (sim.winner == 1 - my_id_) score -= 100.0;
            break;
        }

        // Set our moves
        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s < (int)ind[t].size()) {
                sim.set_dir(alive_ids[s], ind[t][s]);
            }
        }

        // Set opponent moves (predicted or unchanged)
        if (has_opp_moves && t < (int)opp_ind.size()) {
            for (int s = 0; s < (int)opp_alive_ids.size(); s++) {
                if (s < (int)opp_ind[t].size()) {
                    sim.set_dir(opp_alive_ids[s], opp_ind[t][s]);
                }
            }
        }

        sim.step();

        if (cumulative_eval) {
            double weight = eval_decay ? 1.0 / (1.0 + t) : (1.0 + t);
            score += (sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_)) * weight;
        }
    }

    if (!cumulative_eval) {
        score = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_);
    }
    return score;
}

double Bot::evaluate_opp(const SimState& base, const std::vector<int>& opp_alive_ids,
                         const Individual& opp_ind,
                         const std::vector<int>& my_alive_ids) const {
    // Evaluate from opponent's perspective: opponent wants to maximize their score
    SimState sim = base;
    int opp_id = 1 - my_id_;
    int steps = (int)opp_ind.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            if (cumulative_eval) {
                double final_eval = sim.eval(opp_id) + sim.energy_proximity(opp_id, energy_k) - sim.energy_proximity(my_id_, energy_k) + sim.height_advantage(opp_id) - sim.height_advantage(my_id_) + sim.territory(opp_id);
                for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            }
            if (sim.winner == my_id_) score -= 100.0;
            break;
        }

        // Set opponent moves
        for (int s = 0; s < (int)opp_alive_ids.size(); s++) {
            if (s < (int)opp_ind[t].size()) {
                sim.set_dir(opp_alive_ids[s], opp_ind[t][s]);
            }
        }
        // Our snakes just keep their current direction (no moves set)
        (void)my_alive_ids;

        sim.step();

        if (cumulative_eval) {
            double weight = eval_decay ? 1.0 / (1.0 + t) : (1.0 + t);
            score += (sim.eval(opp_id) + sim.energy_proximity(opp_id, energy_k) - sim.energy_proximity(my_id_, energy_k) + sim.height_advantage(opp_id) - sim.height_advantage(my_id_) + sim.territory(opp_id)) * weight;
        }
    }

    if (!cumulative_eval) {
        score = sim.eval(opp_id) + sim.energy_proximity(opp_id, energy_k) - sim.energy_proximity(my_id_, energy_k) + sim.height_advantage(opp_id) - sim.height_advantage(my_id_) + sim.territory(opp_id);
    }
    return score;
}

int Bot::select_parent(const std::vector<ScoredIndividual>& pop) const {
    // Fitness-proportional selection with shifted scores
    double min_score = pop[0].score;
    for (auto& p : pop) {
        if (p.score < min_score) min_score = p.score;
    }

    // Shift so all scores are >= 1.0
    double total = 0.0;
    for (auto& p : pop) {
        total += (p.score - min_score + 1.0);
    }

    double r = (double)rand() / RAND_MAX * total;
    double cumulative = 0.0;
    for (int i = 0; i < (int)pop.size(); i++) {
        cumulative += (pop[i].score - min_score + 1.0);
        if (r <= cumulative) return i;
    }
    return (int)pop.size() - 1;
}

// Generic GA runner used for both our moves and opponent prediction
std::vector<ScoredIndividual> Bot::run_ga(const std::vector<int>& alive_ids,
                       const std::vector<SimDir>& initial_dirs,
                       int ga_depth, Individual* seed,
                       std::chrono::steady_clock::time_point deadline,
                       bool for_opponent,
                       const std::vector<int>& other_alive_ids,
                       const Individual& other_moves,
                       int& generations_out) {
    int num_snakes = (int)alive_ids.size();

    // Initialize population
    std::vector<ScoredIndividual> pop;
    pop.reserve(pop_size);

    // Seed with provided individual
    if (seed && !seed->empty()) {
        Individual shifted;
        for (int t = 1; t < (int)seed->size(); t++) {
            shifted.push_back((*seed)[t]);
        }
        // Pad to ga_depth
        std::vector<SimDir> pad_dirs(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            pad_dirs[s] = shifted.empty() ? initial_dirs[s] : shifted.back()[s];
        }
        while ((int)shifted.size() < ga_depth) {
            std::vector<SimDir> moves(num_snakes);
            for (int s = 0; s < num_snakes; s++) {
                SimDir d = sim_random_dir_no_reverse(pad_dirs[s]);
                moves[s] = d;
                pad_dirs[s] = d;
            }
            shifted.push_back(moves);
        }
        shifted.resize(ga_depth);
        repair(shifted, initial_dirs);

        double score;
        if (for_opponent) {
            score = evaluate_opp(state_, alive_ids, shifted, other_alive_ids);
        } else {
            score = evaluate(state_, alive_ids, shifted, other_alive_ids, other_moves);
        }
        pop.push_back({shifted, score});
    }

    // Fill rest with random individuals
    while ((int)pop.size() < pop_size) {
        auto ind = random_individual(ga_depth, num_snakes, initial_dirs);
        double score;
        if (for_opponent) {
            score = evaluate_opp(state_, alive_ids, ind, other_alive_ids);
        } else {
            score = evaluate(state_, alive_ids, ind, other_alive_ids, other_moves);
        }
        pop.push_back({ind, score});
    }

    // Sort by score descending
    std::sort(pop.begin(), pop.end(),
              [](const ScoredIndividual& a, const ScoredIndividual& b) {
                  return a.score > b.score;
              });

    // Evolve
    int generations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        // Select two parents
        int pa = select_parent(pop);
        int pb = select_parent(pop);
        while (pb == pa && pop.size() > 1) pb = select_parent(pop);

        // Crossover
        Individual child;
        if (num_snakes > 1 && rand() % 2) {
            child = crossover_snake_split(pop[pa].seq, pop[pb].seq,
                                           num_snakes, initial_dirs);
        } else {
            child = crossover_time_split(pop[pa].seq, pop[pb].seq, initial_dirs);
        }

        // Mutate
        mutate(child, initial_dirs);

        // Evaluate
        double score;
        if (for_opponent) {
            score = evaluate_opp(state_, alive_ids, child, other_alive_ids);
        } else {
            score = evaluate(state_, alive_ids, child, other_alive_ids, other_moves);
        }

        // Replace worst if child is better
        if (score > pop.back().score) {
            pop.back() = {child, score};
            std::sort(pop.begin(), pop.end(),
                      [](const ScoredIndividual& a, const ScoredIndividual& b) {
                          return a.score > b.score;
                      });
        }

        generations++;
    }

    generations_out = generations;
    return pop;
}

// ===== Main think loop =====

void Bot::think() {
    auto my_alive = state_.get_alive_ids(my_id_);
    auto opp_alive = state_.get_alive_ids(1 - my_id_);

    if (my_alive.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    auto my_dirs = get_initial_dirs(my_alive);
    auto opp_dirs = get_initial_dirs(opp_alive);

    auto start = std::chrono::steady_clock::now();
    auto hard_deadline = start + std::chrono::milliseconds(38 * cheat_factor);

    // Phase 1: Predict opponent moves — use opp_time_pct of remaining time
    int opp_gens = 0;
    Individual empty_moves;
    if (!opp_alive.empty() && opp_time_pct > 0) {
        auto now = std::chrono::steady_clock::now();
        auto remaining = hard_deadline - now;
        auto opp_budget = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(remaining) * opp_time_pct);
        auto opp_deadline = now + opp_budget;
        auto opp_pop = run_ga(opp_alive, opp_dirs, opp_depth, nullptr,
                              opp_deadline, true, my_alive, empty_moves, opp_gens);
        opp_moves_ = opp_pop[0].seq;
    } else {
        opp_moves_.clear();
    }

    // Phase 2: Run our GA using predicted opponent moves — use all remaining time
    int my_gens = 0;
    auto my_pop = run_ga(my_alive, my_dirs, depth, &prev_best_,
                         hard_deadline, false, opp_alive, opp_moves_, my_gens);

    // Phase 3: Flex — re-evaluate population against random opponent sequences
    int best_idx = 0;
    int flex_tested = 0;

    if (!opp_alive.empty() && flex_count > 0) {
        auto flex_deadline = start + std::chrono::milliseconds(46 * cheat_factor);
        int opp_n = (int)opp_alive.size();

        // Generate random opponent sequences
        std::vector<Individual> opp_seqs;
        opp_seqs.reserve(flex_count);
        for (int i = 0; i < flex_count; i++) {
            opp_seqs.push_back(random_individual(depth, opp_n, opp_dirs));
        }

        // Evaluate each population member against all opponent sequences
        double best_avg = -1e18;
        for (int p = 0; p < (int)my_pop.size(); p++) {
            if (std::chrono::steady_clock::now() >= flex_deadline) break;

            double total = 0.0;
            for (auto& opp_seq : opp_seqs) {
                total += evaluate(state_, my_alive, my_pop[p].seq, opp_alive, opp_seq);
            }
            double avg = total / flex_count;
            if (avg > best_avg) {
                best_avg = avg;
                best_idx = p;
            }
            flex_tested++;
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "t" << turn_ << " d=" << depth
              << " od=" << opp_depth
              << " opp%=" << (int)(opp_time_pct * 100)
              << " pop=" << pop_size
              << " ogens=" << opp_gens
              << " mgens=" << my_gens
              << " flex=" << flex_count
              << " best_idx=" << best_idx
              << " " << elapsed << "ms" << std::endl;

    // Store best for next turn
    prev_best_ = my_pop[best_idx].seq;

    // Output first move
    int num_snakes = (int)my_alive.size();
    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int s = 0; s < num_snakes; s++) {
        if (!out.empty()) out += ";";
        SimDir d = my_pop[best_idx].seq[0][s];
        out += std::to_string(my_alive[s]) + " " + dir_names[d]
             + " gaof_d" + std::to_string(depth)
             + "_od" + std::to_string(opp_depth)
             + "_p" + std::to_string(pop_size)
             + "_f" + std::to_string(flex_count);
    }
    std::cout << out << std::endl;
}

// ===== gaof/main.cpp =====
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
#ifdef BOT_POP
    bot.pop_size = BOT_POP;
#endif
#ifdef BOT_MUTRATE
    bot.mutation_rate = BOT_MUTRATE / 100.0;
#endif
#ifdef BOT_CUMEVAL
    bot.cumulative_eval = BOT_CUMEVAL;
#endif
#ifdef BOT_ENERGY_K
    bot.energy_k = BOT_ENERGY_K;
#endif
#ifdef BOT_EVAL_DECAY
    bot.eval_decay = BOT_EVAL_DECAY;
#endif
#ifdef BOT_CHEAT_FACTOR
    bot.cheat_factor = BOT_CHEAT_FACTOR;
#endif
#ifdef BOT_OPP_DEPTH
    bot.opp_depth = BOT_OPP_DEPTH;
#endif
#ifdef BOT_OPP_TIME_PCT
    bot.opp_time_pct = BOT_OPP_TIME_PCT / 100.0;
#endif
#ifdef BOT_FLEX_COUNT
    bot.flex_count = BOT_FLEX_COUNT;
#endif

    bot.init();

    while (true) {
        bot.read_turn();
        bot.think();
    }

    return 0;
}
