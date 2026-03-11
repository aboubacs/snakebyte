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

double Bot::simulate(const SimState& base, const std::vector<int>& alive_ids,
                     const std::vector<std::vector<SimDir>>& seq) const {
    SimState sim = base;
    int steps = (int)seq.size();
    double score = 0.0;

    for (int t = 0; t < steps; t++) {
        if (sim.game_over) {
            double final_eval = sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_);
            for (int r = t; r < steps; r++) score += final_eval * (eval_decay ? 1.0 / (1.0 + r) : (1.0 + r));
            if (sim.winner == 1 - my_id_) score -= 100.0;
            break;
        }

        // Apply our moves
        for (int s = 0; s < (int)alive_ids.size(); s++) {
            if (s < (int)seq[t].size()) {
                sim.set_dir(alive_ids[s], seq[t][s]);
            }
        }

        // Opponent doesn't move (keeps current direction)
        sim.step();

        // Accumulate eval at each step, weighted so earlier steps matter more
        double weight = eval_decay ? 1.0 / (1.0 + t) : (1.0 + t);
        score += (sim.eval(my_id_) + sim.energy_proximity(my_id_, energy_k) - sim.energy_proximity(1 - my_id_, energy_k) + sim.height_advantage(my_id_) - sim.height_advantage(1 - my_id_) + sim.territory(my_id_)) * weight;
    }

    return score;
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
    auto deadline = start + std::chrono::milliseconds(40 * cheat_factor);

    double best_eval = -999999.0;
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

        double eval = simulate(state_, alive_ids, shifted);
        if (eval > best_eval) {
            best_eval = eval;
            best_seq = shifted;
        }
    }

    // Monte Carlo: generate random sequences and keep the best
    int iterations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        auto seq = random_sequence(rollout_depth, num_snakes, alive_ids);
        double eval = simulate(state_, alive_ids, seq);

        if (eval > best_eval) {
            best_eval = eval;
            best_seq = seq;
        }
        iterations++;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "t" << turn_ << " d=" << rollout_depth
              << " iters=" << iterations << " best=" << best_eval
              << " " << elapsed << "ms" << std::endl;

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
