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
                step[s] = prev[s];
            }
            prev[s] = step[s];
        }
    }
}

Individual Bot::crossover_time_split(const Individual& a, const Individual& b,
                                      const std::vector<SimDir>& initial_dirs) const {
    int len = (int)a.size();
    int split = 1 + rand() % (len - 1);
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
                SimDir prev = (t > 0) ? ind[t - 1][s] : initial_dirs[s];
                ind[t][s] = sim_random_dir_no_reverse(prev);
                mutated = true;
            }
        }
    }

    if (mutated) {
        repair(ind, initial_dirs);
    }
}

double Bot::evaluate(const SimState& base, const std::vector<int>& alive_ids,
                     const Individual& ind) const {
    SimState sim = base;
    int steps = (int)ind.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            if (cumulative_eval) {
                double final_eval = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_);
                for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            }
            if (sim.winner == 1 - my_id_) score -= 100.0;
            break;
        }

        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s < (int)ind[t].size()) {
                sim.set_dir(alive_ids[s], ind[t][s]);
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

double Bot::evaluate_vs(const SimState& base, const std::vector<int>& alive_ids,
                        const Individual& ind,
                        const std::vector<int>& opp_alive_ids,
                        const Individual& opp_ind) const {
    SimState sim = base;
    int steps = (int)ind.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            if (cumulative_eval) {
                double final_eval = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_);
                for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            }
            if (sim.winner == 1 - my_id_) score -= 100.0;
            break;
        }

        // Our moves
        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s < (int)ind[t].size()) {
                sim.set_dir(alive_ids[s], ind[t][s]);
            }
        }

        // Opponent moves
        if (t < (int)opp_ind.size()) {
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

int Bot::select_parent(const std::vector<ScoredIndividual>& pop) const {
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
    auto opp_alive = state_.get_alive_ids(1 - my_id_);

    if (alive_ids.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    int num_snakes = (int)alive_ids.size();
    auto initial_dirs = get_initial_dirs(alive_ids);

    auto start = std::chrono::steady_clock::now();
    auto ga_deadline = start + std::chrono::milliseconds(38 * cheat_factor);

    // Initialize population
    std::vector<ScoredIndividual> pop;
    pop.reserve(pop_size);

    // Seed with shifted previous best
    if (!prev_best_.empty()) {
        Individual shifted;
        for (int t = 1; t < (int)prev_best_.size(); t++) {
            shifted.push_back(prev_best_[t]);
        }
        std::vector<SimDir> pad_dirs(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            pad_dirs[s] = shifted.empty() ? initial_dirs[s] : shifted.back()[s];
        }
        while ((int)shifted.size() < depth) {
            std::vector<SimDir> moves(num_snakes);
            for (int s = 0; s < num_snakes; s++) {
                SimDir d = sim_random_dir_no_reverse(pad_dirs[s]);
                moves[s] = d;
                pad_dirs[s] = d;
            }
            shifted.push_back(moves);
        }
        shifted.resize(depth);
        repair(shifted, initial_dirs);

        double score = evaluate(state_, alive_ids, shifted);
        pop.push_back({shifted, score});
    }

    // Fill rest with random individuals
    while ((int)pop.size() < pop_size) {
        auto ind = random_individual(depth, num_snakes, initial_dirs);
        double score = evaluate(state_, alive_ids, ind);
        pop.push_back({ind, score});
    }

    // Sort by score descending
    std::sort(pop.begin(), pop.end(),
              [](const ScoredIndividual& a, const ScoredIndividual& b) {
                  return a.score > b.score;
              });

    // Evolve
    int generations = 0;
    while (std::chrono::steady_clock::now() < ga_deadline) {
        int pa = select_parent(pop);
        int pb = select_parent(pop);
        while (pb == pa && pop.size() > 1) pb = select_parent(pop);

        Individual child;
        if (num_snakes > 1 && rand() % 2) {
            child = crossover_snake_split(pop[pa].seq, pop[pb].seq,
                                           num_snakes, initial_dirs);
        } else {
            child = crossover_time_split(pop[pa].seq, pop[pb].seq, initial_dirs);
        }

        mutate(child, initial_dirs);

        double score = evaluate(state_, alive_ids, child);

        if (score > pop.back().score) {
            pop.back() = {child, score};
            std::sort(pop.begin(), pop.end(),
                      [](const ScoredIndividual& a, const ScoredIndividual& b) {
                          return a.score > b.score;
                      });
        }

        generations++;
    }

    // ===== Flex phase: re-evaluate against random opponent sequences =====
    int best_idx = 0;
    int flex_tested = 0;

    if (!opp_alive.empty() && flex_count > 0) {
        auto flex_deadline = start + std::chrono::milliseconds(46 * cheat_factor);
        auto opp_dirs = get_initial_dirs(opp_alive);
        int opp_n = (int)opp_alive.size();

        // Generate random opponent sequences
        std::vector<Individual> opp_seqs;
        opp_seqs.reserve(flex_count);
        for (int i = 0; i < flex_count; i++) {
            opp_seqs.push_back(random_individual(depth, opp_n, opp_dirs));
        }

        // Evaluate each population member against all opponent sequences
        double best_avg = -1e18;
        for (int p = 0; p < (int)pop.size(); p++) {
            if (std::chrono::steady_clock::now() >= flex_deadline) break;

            double total = 0.0;
            for (auto& opp_seq : opp_seqs) {
                total += evaluate_vs(state_, alive_ids, pop[p].seq, opp_alive, opp_seq);
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
              << " pop=" << pop_size
              << " gens=" << generations
              << " flex_seqs=" << flex_count
              << " flex_tested=" << flex_tested
              << " best_idx=" << best_idx
              << " " << elapsed << "ms" << std::endl;

    // Store best for next turn
    prev_best_ = pop[best_idx].seq;

    // Output first move
    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int s = 0; s < num_snakes; s++) {
        if (!out.empty()) out += ";";
        SimDir d = pop[best_idx].seq[0][s];
        out += std::to_string(alive_ids[s]) + " " + dir_names[d]
             + " gaf_d" + std::to_string(depth)
             + "_p" + std::to_string(pop_size)
             + "_f" + std::to_string(flex_count);
    }
    std::cout << out << std::endl;
}
