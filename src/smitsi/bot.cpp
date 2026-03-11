#include "bot.hpp"

// Helper: get the 3 valid (non-reverse) directions from a parent direction
static void get_valid_dirs(SimDir parent_dir, SimDir out[3]) {
    SimDir opp = sim_opposite(parent_dir);
    int idx = 0;
    for (int d = 0; d < 4; d++) {
        if ((SimDir)d != opp) {
            out[idx++] = (SimDir)d;
        }
    }
}

static SimDir parse_body_direction(const SimPos& head, const SimPos& neck) {
    int dx = head.x - neck.x;
    int dy = head.y - neck.y;
    if (dy < 0) return SIM_UP;
    if (dy > 0) return SIM_DOWN;
    if (dx < 0) return SIM_LEFT;
    if (dx > 0) return SIM_RIGHT;
    return SIM_UP;
}

// ===== SmitsiTree =====

void SmitsiTree::init(SimDir initial_dir, int sid, int own) {
    nodes.clear();
    nodes.reserve(30000);
    SmitsiNode root;
    root.dir = initial_dir;
    root.parent = -1;
    root.visits = 0;
    root.total_score = 0.0;
    root.children[0] = root.children[1] = root.children[2] = -1;
    nodes.push_back(root);
    snake_id = sid;
    owner = own;
    path_len = 0;
    low_score = 1e18;
    high_score = -1e18;
}

void SmitsiTree::expand(int idx) {
    SimDir valid[3];
    get_valid_dirs(nodes[idx].dir, valid);
    for (int i = 0; i < 3; i++) {
        int c = (int)nodes.size();
        SmitsiNode node;
        node.dir = valid[i];
        node.parent = idx;
        node.visits = 0;
        node.total_score = 0.0;
        node.children[0] = node.children[1] = node.children[2] = -1;
        nodes.push_back(node);
        nodes[idx].children[i] = c;
    }
}

int SmitsiTree::select(int parent_idx, double C, int rand_thresh) {
    // Expand if needed
    if (nodes[parent_idx].children[0] == -1) {
        expand(parent_idx);
    }

    // Random selection for first few visits to avoid early convergence on bad moves
    if (nodes[parent_idx].visits < rand_thresh) {
        int r = rand() % 3;
        int child = nodes[parent_idx].children[r];
        nodes[child].visits++;
        return child;
    }

    // Visit unvisited children first
    for (int i = 0; i < 3; i++) {
        int c = nodes[parent_idx].children[i];
        if (nodes[c].visits == 0) {
            nodes[c].visits++;
            return c;
        }
    }

    // UCB selection
    double scale = high_score - low_score;
    if (scale < 1e-9) scale = 1.0;

    double log_parent = log((double)nodes[parent_idx].visits);
    double best_ucb = -1e18;
    int best = -1;

    for (int i = 0; i < 3; i++) {
        int c = nodes[parent_idx].children[i];
        double avg = nodes[c].total_score / nodes[c].visits;
        double exploit = (avg - low_score) / scale;
        double explore = C * sqrt(log_parent / nodes[c].visits);
        double ucb = exploit + explore;
        if (ucb > best_ucb) {
            best_ucb = ucb;
            best = c;
        }
    }

    nodes[best].visits++;
    return best;
}

void SmitsiTree::backpropagate(double score) {
    if (score < low_score) low_score = score;
    if (score > high_score) high_score = score;
    nodes[0].visits++;
    for (int i = 0; i < path_len; i++) {
        nodes[path[i]].total_score += score;
    }
}

SimDir SmitsiTree::best_move() const {
    // Pick child of root with most visits
    int best = -1;
    int best_visits = -1;
    for (int i = 0; i < 3; i++) {
        int c = nodes[0].children[i];
        if (c != -1 && nodes[c].visits > best_visits) {
            best_visits = nodes[c].visits;
            best = c;
        }
    }
    return best >= 0 ? nodes[best].dir : nodes[0].dir;
}

// ===== Bot =====

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

double Bot::compute_eval(const SimState& sim, int player) const {
    return sim.eval(player)
         + sim.energy_proximity(player, energy_k)
         - sim.energy_proximity(1 - player, energy_k)
         + sim.height_advantage(player)
         - sim.height_advantage(1 - player)
         + sim.territory(player);
}

void Bot::think() {
    auto my_alive = state_.get_alive_ids(my_id_);
    auto opp_alive = state_.get_alive_ids(1 - my_id_);

    if (my_alive.empty()) {
        std::cout << "WAIT" << std::endl;
        return;
    }

    // Create one tree per snake
    std::vector<SmitsiTree> trees;
    int my_tree_count = (int)my_alive.size();

    for (int id : my_alive) {
        SmitsiTree t;
        const SimSnake* sn = state_.get_snake(id);
        t.init(sn ? sn->dir : SIM_UP, id, my_id_);
        trees.push_back(std::move(t));
    }
    for (int id : opp_alive) {
        SmitsiTree t;
        const SimSnake* sn = state_.get_snake(id);
        t.init(sn ? sn->dir : SIM_UP, id, 1 - my_id_);
        trees.push_back(std::move(t));
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::milliseconds(38 * cheat_factor);

    int iterations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        SimState sim = state_;

        // Reset paths
        for (auto& tree : trees) tree.path_len = 0;

        double my_score = 0.0;
        double opp_score = 0.0;

        for (int d = 0; d < depth; d++) {
            if (sim.game_over) {
                // Pad remaining steps with final eval
                double my_final = compute_eval(sim, my_id_);
                double opp_final = compute_eval(sim, 1 - my_id_);
                for (int r = d; r < depth; r++) {
                    double w = eval_decay ? 1.0 / (1.0 + r) : (1.0 + r);
                    my_score += my_final * w;
                    opp_score += opp_final * w;
                }
                if (sim.winner == 1 - my_id_) my_score -= 100.0;
                if (sim.winner == my_id_) opp_score -= 100.0;
                break;
            }

            // Select moves for all snakes from their respective trees
            for (auto& tree : trees) {
                int parent = (d == 0) ? 0 : tree.path[d - 1];
                int child = tree.select(parent, explore_c, random_threshold);
                tree.path[tree.path_len++] = child;
                sim.set_dir(tree.snake_id, tree.nodes[child].dir);
            }

            sim.step();

            double w = eval_decay ? 1.0 / (1.0 + d) : (1.0 + d);
            my_score += compute_eval(sim, my_id_) * w;
            opp_score += compute_eval(sim, 1 - my_id_) * w;
        }

        // Backpropagate: my trees get my_score, opponent trees get opp_score
        for (auto& tree : trees) {
            double score = (tree.owner == my_id_) ? my_score : opp_score;
            tree.backpropagate(score);
        }

        iterations++;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cerr << "t" << turn_ << " d=" << depth
              << " trees=" << (int)trees.size()
              << " iters=" << iterations
              << " " << elapsed << "ms" << std::endl;

    // Output first move for each of my snakes
    const char* dir_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};
    std::string out;
    for (int i = 0; i < my_tree_count; i++) {
        if (!out.empty()) out += ";";
        SimDir d = trees[i].best_move();
        out += std::to_string(my_alive[i]) + " " + dir_names[d]
             + " smitsi_d" + std::to_string(depth)
             + "_i" + std::to_string(iterations);
    }
    std::cout << out << std::endl;
}
