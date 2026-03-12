#include "sim.hpp"

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
        if (s->length() > 3) {
            s->body.erase(s->body.begin());
        } else {
            s->alive = false;
        }
    }
}

void SimState::apply_gravity() {
    // Flat grounded array indexed by snake index (not id)
    const int n = (int)snakes.size();
    bool grounded[16]; // max 16 snakes (8 per player, 2 players)

    // Grid bitmap for grounded snake body positions
    // Avoids O(snakes * body) lookup per cell
    bool body_grid[50][50]; // max grid 50x50

    bool something_fell = true;
    while (something_fell) {
        something_fell = false;

        // Reset grounded flags
        for (int i = 0; i < n; i++) grounded[i] = false;

        bool something_got_grounded = true;
        while (something_got_grounded) {
            something_got_grounded = false;

            // Build grid of grounded snake body positions
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                    body_grid[y][x] = false;
            for (int i = 0; i < n; i++) {
                if (!snakes[i].alive || !grounded[i]) continue;
                for (auto& bp : snakes[i].body) {
                    body_grid[bp.y][bp.x] = true;
                }
            }

            for (int i = 0; i < n; i++) {
                if (!snakes[i].alive || grounded[i]) continue;
                bool is_grounded = false;
                for (auto& bp : snakes[i].body) {
                    int bx = bp.x, by = bp.y + 1;
                    if (by < height && is_platform({bx, by})) { is_grounded = true; break; }
                    if (has_energy({bx, by})) { is_grounded = true; break; }
                    if (bx >= 0 && bx < width && by >= 0 && by < height && body_grid[by][bx]) {
                        is_grounded = true; break;
                    }
                }
                if (is_grounded) {
                    grounded[i] = true;
                    something_got_grounded = true;
                }
            }
        }

        // Drop all airborne snakes by 1
        for (int i = 0; i < n; i++) {
            if (!snakes[i].alive || grounded[i]) continue;
            for (auto& bp : snakes[i].body) {
                bp.y += 1;
            }
            something_fell = true;
        }

        // Kill snakes that fell off the grid
        for (int i = 0; i < n; i++) {
            if (!snakes[i].alive) continue;
            for (auto& bp : snakes[i].body) {
                if (!in_bounds(bp)) {
                    snakes[i].alive = false;
                    something_fell = true;
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
