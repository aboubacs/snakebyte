#include "bot.hpp"

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

DIndividual Bot::random_individual(int len, SimDir initial_dir) const {
    DIndividual ind(len);
    SimDir cur = initial_dir;
    for (int t = 0; t < len; t++) {
        SimDir d = sim_random_dir_no_reverse(cur);
        ind[t] = d;
        cur = d;
    }
    return ind;
}

void Bot::repair(DIndividual& ind, SimDir initial_dir) const {
    SimDir prev = initial_dir;
    for (auto& d : ind) {
        if (d == sim_opposite(prev)) {
            d = prev;
        }
        prev = d;
    }
}

DIndividual Bot::crossover(const DIndividual& a, const DIndividual& b,
                            SimDir initial_dir) const {
    int len = (int)a.size();
    int split = 1 + rand() % (len - 1);
    DIndividual child(len);
    for (int t = 0; t < len; t++) {
        child[t] = (t < split) ? a[t] : b[t];
    }
    repair(child, initial_dir);
    return child;
}

void Bot::mutate(DIndividual& ind, SimDir initial_dir) const {
    bool mutated = false;
    for (int t = 0; t < (int)ind.size(); t++) {
        if ((rand() % 1000) < (int)(mutation_rate * 1000)) {
            SimDir prev = (t > 0) ? ind[t - 1] : initial_dir;
            ind[t] = sim_random_dir_no_reverse(prev);
            mutated = true;
        }
    }
    if (mutated) {
        repair(ind, initial_dir);
    }
}

double Bot::evaluate(const SimState& base,
                     const std::vector<int>& alive_ids,
                     int snake_idx,
                     const DIndividual& ind,
                     const std::vector<DIndividual>& fixed_moves) const {
    SimState sim = base;
    int steps = (int)ind.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) break;

        // Set moves for all our alive snakes
        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s == snake_idx) {
                sim.set_dir(alive_ids[s], ind[t]);
            } else if (s < (int)fixed_moves.size() && t < (int)fixed_moves[s].size()) {
                sim.set_dir(alive_ids[s], fixed_moves[s][t]);
            }
            // else: snake keeps its current dir (not yet optimized)
        }

        sim.step();

        if (cumulative_eval) {
            double weight = 1.0 + t;
            score += (sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k)) * weight;
        }
    }

    if (!cumulative_eval) {
        score = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k);
    }
    return score;
}

int Bot::select_parent(const std::vector<DScoredIndividual>& pop) const {
    double min_score = pop[0].score;
    for (auto& p : pop) {
        if (p.score < min_score) min_score = p.score;
    }

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

// ===== Main think loop =====

void Bot::think() {
    auto alive_ids = state_.get_alive_ids(my_id_);

    if (alive_ids.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    int num_snakes = (int)alive_ids.size();

    auto start = std::chrono::steady_clock::now();
    auto hard_deadline = start + std::chrono::milliseconds(38);

    // fixed_moves[s] holds the best moves found so far for snake s
    // For already-optimized snakes: their GA result
    // For not-yet-optimized snakes: keep current direction (empty = use default dir)
    std::vector<DIndividual> fixed_moves(num_snakes);

    // Initialize with "keep current direction" for all
    for (int s = 0; s < num_snakes; s++) {
        const SimSnake* sn = state_.get_snake(alive_ids[s]);
        SimDir dir = sn ? sn->dir : SIM_UP;
        fixed_moves[s].assign(depth, dir);
    }

    int total_gens = 0;

    // Optimize each snake sequentially
    for (int si = 0; si < num_snakes; si++) {
        const SimSnake* sn = state_.get_snake(alive_ids[si]);
        SimDir initial_dir = sn ? sn->dir : SIM_UP;

        // Each snake gets 1/(remaining snakes) of remaining time
        auto now = std::chrono::steady_clock::now();
        int remaining_snakes = num_snakes - si;
        auto remaining_time = hard_deadline - now;
        auto snake_time = remaining_time / remaining_snakes;
        auto deadline = now + snake_time;

        // Initialize population
        std::vector<DScoredIndividual> pop;
        pop.reserve(pop_size);

        // Seed with shifted previous best for this snake
        if (si < (int)prev_best_.size() && !prev_best_[si].empty()) {
            DIndividual shifted;
            for (int t = 1; t < (int)prev_best_[si].size(); t++) {
                shifted.push_back(prev_best_[si][t]);
            }
            SimDir pad_dir = shifted.empty() ? initial_dir : shifted.back();
            while ((int)shifted.size() < depth) {
                SimDir d = sim_random_dir_no_reverse(pad_dir);
                shifted.push_back(d);
                pad_dir = d;
            }
            shifted.resize(depth);
            repair(shifted, initial_dir);

            double score = evaluate(state_, alive_ids, si, shifted, fixed_moves);
            pop.push_back({shifted, score});
        }

        // Fill rest with random
        while ((int)pop.size() < pop_size) {
            auto ind = random_individual(depth, initial_dir);
            double score = evaluate(state_, alive_ids, si, ind, fixed_moves);
            pop.push_back({ind, score});
        }

        std::sort(pop.begin(), pop.end(),
                  [](const DScoredIndividual& a, const DScoredIndividual& b) {
                      return a.score > b.score;
                  });

        // Evolve
        int generations = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            int pa = select_parent(pop);
            int pb = select_parent(pop);
            while (pb == pa && pop.size() > 1) pb = select_parent(pop);

            DIndividual child = crossover(pop[pa].seq, pop[pb].seq, initial_dir);
            mutate(child, initial_dir);

            double score = evaluate(state_, alive_ids, si, child, fixed_moves);

            if (score > pop.back().score) {
                pop.back() = {child, score};
                std::sort(pop.begin(), pop.end(),
                          [](const DScoredIndividual& a, const DScoredIndividual& b) {
                              return a.score > b.score;
                          });
            }
            generations++;
        }

        total_gens += generations;
        fixed_moves[si] = pop[0].seq;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "t" << turn_ << " d=" << depth
              << " pop=" << pop_size
              << " snakes=" << num_snakes
              << " gens=" << total_gens
              << " " << elapsed << "ms" << std::endl;

    // Store best for next turn
    prev_best_ = fixed_moves;

    // Output first move
    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int s = 0; s < num_snakes; s++) {
        if (!out.empty()) out += ";";
        SimDir d = fixed_moves[s][0];
        out += std::to_string(alive_ids[s]) + " " + dir_names[d]
             + " dga_d" + std::to_string(depth)
             + "_p" + std::to_string(pop_size);
    }
    std::cout << out << std::endl;
}
