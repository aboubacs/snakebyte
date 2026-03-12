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

double Bot::evaluate_matchup(const SimState& base,
                              const std::vector<int>& my_alive,
                              const Individual& my_ind,
                              const std::vector<int>& opp_alive,
                              const Individual& opp_ind,
                              int eval_player) const {
    SimState sim = base;
    int steps = (int)my_ind.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            if (cumulative_eval) {
                double final_eval = sim.eval(eval_player) + sim.energy_proximity(eval_player, energy_k) - sim.energy_proximity(1 - eval_player, energy_k) + sim.height_advantage(eval_player) - sim.height_advantage(1 - eval_player) + sim.territory(eval_player) + center_control_factor * (sim.center_control(eval_player) - sim.center_control(1 - eval_player));
                for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            }
            if (sim.winner == 1 - eval_player) score -= 100.0;
            break;
        }

        // Set our moves
        for (int s = 0; s < (int)my_alive.size(); s++) {
            if (s < (int)my_ind[t].size()) {
                sim.set_dir(my_alive[s], my_ind[t][s]);
            }
        }

        // Set opponent moves
        if (t < (int)opp_ind.size()) {
            for (int s = 0; s < (int)opp_alive.size(); s++) {
                if (s < (int)opp_ind[t].size()) {
                    sim.set_dir(opp_alive[s], opp_ind[t][s]);
                }
            }
        }

        sim.step();

        if (cumulative_eval) {
            double weight = eval_decay ? 1.0 / (1.0 + t) : (1.0 + t);
            score += (sim.eval(eval_player) + sim.energy_proximity(eval_player, energy_k) - sim.energy_proximity(1 - eval_player, energy_k) + sim.height_advantage(eval_player) - sim.height_advantage(1 - eval_player) + sim.territory(eval_player) + center_control_factor * (sim.center_control(eval_player) - sim.center_control(1 - eval_player))) * weight;
        }
    }

    if (!cumulative_eval) {
        score = sim.eval(eval_player) + sim.energy_proximity(eval_player, energy_k) - sim.energy_proximity(1 - eval_player, energy_k) + sim.height_advantage(eval_player) - sim.height_advantage(1 - eval_player) + sim.territory(eval_player) + center_control_factor * (sim.center_control(eval_player) - sim.center_control(1 - eval_player));
    }
    return score;
}

double Bot::score_against_elite(const SimState& base,
                                 const std::vector<int>& my_alive,
                                 const Individual& ind,
                                 const std::vector<int>& opp_alive,
                                 const std::vector<ScoredIndividual>& opp_elite,
                                 int eval_player) const {
    if (opp_elite.empty()) {
        // No opponents yet, evaluate with opponent holding direction
        Individual empty_opp;
        return evaluate_matchup(base, my_alive, ind, opp_alive, empty_opp, eval_player);
    }

    // Average score against elite opponents (worst-case would be min, but avg is more stable)
    double total = 0.0;
    int k = std::min(elite_k, (int)opp_elite.size());
    for (int i = 0; i < k; i++) {
        total += evaluate_matchup(base, my_alive, ind, opp_alive, opp_elite[i].seq, eval_player);
    }
    return total / k;
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

// Helper: shift an individual from previous turn, pad to depth
static Individual shift_and_pad(const Individual& prev, int target_depth,
                                 int num_snakes, const std::vector<SimDir>& initial_dirs) {
    Individual shifted;
    for (int t = 1; t < (int)prev.size(); t++) {
        shifted.push_back(prev[t]);
    }
    std::vector<SimDir> pad_dirs(num_snakes);
    for (int s = 0; s < num_snakes; s++) {
        pad_dirs[s] = shifted.empty() ? initial_dirs[s] : shifted.back()[s];
    }
    while ((int)shifted.size() < target_depth) {
        std::vector<SimDir> moves(num_snakes);
        for (int s = 0; s < num_snakes; s++) {
            SimDir d = sim_random_dir_no_reverse(pad_dirs[s]);
            moves[s] = d;
            pad_dirs[s] = d;
        }
        shifted.push_back(moves);
    }
    shifted.resize(target_depth);
    return shifted;
}

// ===== Main think loop =====

void Bot::think() {
    auto my_alive = state_.get_alive_ids(my_id_);
    auto opp_alive = state_.get_alive_ids(1 - my_id_);

    if (my_alive.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    int my_n = (int)my_alive.size();
    int opp_n = (int)opp_alive.size();
    auto my_dirs = get_initial_dirs(my_alive);
    auto opp_dirs = get_initial_dirs(opp_alive);

    auto start = std::chrono::steady_clock::now();
    auto hard_deadline = start + std::chrono::milliseconds(38 * cheat_factor);

    // --- Initialize my population ---
    std::vector<ScoredIndividual> my_pop;
    my_pop.reserve(pop_size);

    if (!prev_best_my_.empty()) {
        auto shifted = shift_and_pad(prev_best_my_, depth, my_n, my_dirs);
        repair(shifted, my_dirs);
        my_pop.push_back({shifted, 0.0});
    }
    while ((int)my_pop.size() < pop_size) {
        my_pop.push_back({random_individual(depth, my_n, my_dirs), 0.0});
    }

    // --- Initialize opponent population ---
    std::vector<ScoredIndividual> opp_pop;
    opp_pop.reserve(pop_size);

    if (!prev_best_opp_.empty() && opp_n > 0) {
        auto shifted = shift_and_pad(prev_best_opp_, depth, opp_n, opp_dirs);
        repair(shifted, opp_dirs);
        opp_pop.push_back({shifted, 0.0});
    }
    if (opp_n > 0) {
        while ((int)opp_pop.size() < pop_size) {
            opp_pop.push_back({random_individual(depth, opp_n, opp_dirs), 0.0});
        }
    }

    // --- Initial scoring: score my pop with no opponent elite yet ---
    // Score opponent pop first so we have elite for my pop
    int opp_id = 1 - my_id_;
    std::vector<ScoredIndividual> my_elite, opp_elite;

    // Bootstrap: score opp against empty my elite
    for (auto& ind : opp_pop) {
        ind.score = score_against_elite(state_, opp_alive, ind.seq, my_alive, my_elite, opp_id);
    }
    std::sort(opp_pop.begin(), opp_pop.end(),
              [](const ScoredIndividual& a, const ScoredIndividual& b) { return a.score > b.score; });
    opp_elite.assign(opp_pop.begin(), opp_pop.begin() + std::min(elite_k, (int)opp_pop.size()));

    // Score my pop against opp elite
    for (auto& ind : my_pop) {
        ind.score = score_against_elite(state_, my_alive, ind.seq, opp_alive, opp_elite, my_id_);
    }
    std::sort(my_pop.begin(), my_pop.end(),
              [](const ScoredIndividual& a, const ScoredIndividual& b) { return a.score > b.score; });
    my_elite.assign(my_pop.begin(), my_pop.begin() + std::min(elite_k, (int)my_pop.size()));

    // --- Coevolution loop: alternate generations ---
    int my_gens = 0, opp_gens = 0;
    bool evolve_mine = true;  // alternate

    while (std::chrono::steady_clock::now() < hard_deadline) {
        if (evolve_mine || opp_pop.empty()) {
            // Evolve my population
            int pa = select_parent(my_pop);
            int pb = select_parent(my_pop);
            while (pb == pa && my_pop.size() > 1) pb = select_parent(my_pop);

            Individual child;
            if (my_n > 1 && rand() % 2) {
                child = crossover_snake_split(my_pop[pa].seq, my_pop[pb].seq, my_n, my_dirs);
            } else {
                child = crossover_time_split(my_pop[pa].seq, my_pop[pb].seq, my_dirs);
            }
            mutate(child, my_dirs);

            double score = score_against_elite(state_, my_alive, child, opp_alive, opp_elite, my_id_);

            if (score > my_pop.back().score) {
                my_pop.back() = {child, score};
                std::sort(my_pop.begin(), my_pop.end(),
                          [](const ScoredIndividual& a, const ScoredIndividual& b) { return a.score > b.score; });
                my_elite.assign(my_pop.begin(), my_pop.begin() + std::min(elite_k, (int)my_pop.size()));
            }
            my_gens++;
        } else {
            // Evolve opponent population
            int pa = select_parent(opp_pop);
            int pb = select_parent(opp_pop);
            while (pb == pa && opp_pop.size() > 1) pb = select_parent(opp_pop);

            Individual child;
            if (opp_n > 1 && rand() % 2) {
                child = crossover_snake_split(opp_pop[pa].seq, opp_pop[pb].seq, opp_n, opp_dirs);
            } else {
                child = crossover_time_split(opp_pop[pa].seq, opp_pop[pb].seq, opp_dirs);
            }
            mutate(child, opp_dirs);

            double score = score_against_elite(state_, opp_alive, child, my_alive, my_elite, opp_id);

            if (score > opp_pop.back().score) {
                opp_pop.back() = {child, score};
                std::sort(opp_pop.begin(), opp_pop.end(),
                          [](const ScoredIndividual& a, const ScoredIndividual& b) { return a.score > b.score; });
                opp_elite.assign(opp_pop.begin(), opp_pop.begin() + std::min(elite_k, (int)opp_pop.size()));
            }
            opp_gens++;
        }

        evolve_mine = !evolve_mine;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "t" << turn_ << " d=" << depth
              << " pop=" << pop_size
              << " k=" << elite_k
              << " mgens=" << my_gens
              << " ogens=" << opp_gens
              << " " << elapsed << "ms" << std::endl;

    // Store best for next turn
    prev_best_my_ = my_pop[0].seq;
    if (!opp_pop.empty()) {
        prev_best_opp_ = opp_pop[0].seq;
    }

    // Output first move
    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int s = 0; s < my_n; s++) {
        if (!out.empty()) out += ";";
        SimDir d = my_pop[0].seq[0][s];
        out += std::to_string(my_alive[s]) + " " + dir_names[d]
             + " cga_d" + std::to_string(depth)
             + "_p" + std::to_string(pop_size)
             + "_k" + std::to_string(elite_k)
             + "_g" + std::to_string(my_gens);
    }
    std::cout << out << std::endl;
}
