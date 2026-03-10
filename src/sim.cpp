#include "sim.hpp"

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
