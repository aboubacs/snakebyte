#include "referee.hpp"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <set>
#include <queue>
#include <cmath>
#include <unistd.h>
#include <numeric>
#include <functional>

// ===== Map generation helpers =====

static double rand_double() {
    return (double)rand() / RAND_MAX;
}

// Flood fill from (sx, sy) on empty cells, returns set of positions
static std::set<Pos> flood_fill_empty(const std::vector<std::string>& grid, int w, int h, int sx, int sy, const std::set<Pos>& visited) {
    std::set<Pos> region;
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return region;
    if (grid[sy][sx] != '.') return region;
    Pos start = {sx, sy};
    if (visited.count(start)) return region;

    std::queue<Pos> q;
    q.push(start);
    region.insert(start);

    while (!q.empty()) {
        Pos p = q.front(); q.pop();
        const int dx[] = {0, 0, -1, 1};
        const int dy[] = {-1, 1, 0, 0};
        for (int d = 0; d < 4; d++) {
            Pos np = {p.x + dx[d], p.y + dy[d]};
            if (np.x < 0 || np.x >= w || np.y < 0 || np.y >= h) continue;
            if (grid[np.y][np.x] != '.') continue;
            if (region.count(np) || visited.count(np)) continue;
            region.insert(np);
            q.push(np);
        }
    }
    return region;
}

// Flood fill walls from (sx, sy), returns set of wall positions
static std::set<Pos> flood_fill_walls(const std::vector<std::string>& grid, int w, int h, int sx, int sy, const std::set<Pos>& visited) {
    std::set<Pos> region;
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return region;
    if (grid[sy][sx] != '#') return region;
    Pos start = {sx, sy};
    if (visited.count(start)) return region;

    std::queue<Pos> q;
    q.push(start);
    region.insert(start);

    while (!q.empty()) {
        Pos p = q.front(); q.pop();
        const int dx[] = {0, 0, -1, 1};
        const int dy[] = {-1, 1, 0, 0};
        for (int d = 0; d < 4; d++) {
            Pos np = {p.x + dx[d], p.y + dy[d]};
            if (np.x < 0 || np.x >= w || np.y < 0 || np.y >= h) continue;
            if (grid[np.y][np.x] != '#') continue;
            if (region.count(np) || visited.count(np)) continue;
            region.insert(np);
            q.push(np);
        }
    }
    return region;
}

static int count_wall_neighbors_4(const std::vector<std::string>& grid, int w, int h, int x, int y) {
    int count = 0;
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
            count++;  // Out of bounds counts as wall
            continue;
        }
        if (grid[ny][nx] == '#') count++;
    }
    return count;
}

static int count_wall_neighbors_8(const std::vector<std::string>& grid, int w, int h, int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (grid[ny][nx] == '#') count++;
        }
    }
    return count;
}

// ===== Map generation (ported from Java GridMaker) =====

// Check if cell (x,y) has 8-adjacency to any position in the set
static bool adjacent8_to_set(int x, int y, const std::set<Pos>& positions) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (positions.count({x + dx, y + dy})) return true;
        }
    }
    return false;
}

// getFreeAbove: count consecutive free (non-wall) cells above (x, y) exclusive
static int get_free_above(const std::vector<std::string>& grid, int x, int y) {
    int count = 0;
    for (int ny = y - 1; ny >= 0; ny--) {
        if (grid[ny][x] == '#') break;
        count++;
    }
    return count;
}

void GameState::init_default_map() {
    srand(time(nullptr) ^ getpid());

    // Height: MIN=10, MAX=24, skew=0.3
    double skew = 0.3;
    height = 10 + (int)round(pow(rand_double(), skew) * (24 - 10));
    if (height < 10) height = 10;
    if (height > 24) height = 24;
    width = (int)round(height * 1.8);
    if (width % 2 != 0) width++;

    grid.assign(height, std::string(width, '.'));

    // Bottom row is all walls
    grid[height - 1] = std::string(width, '#');

    // Probabilistic wall placement across FULL width
    double b = 5.0 + rand_double() * 10.0;
    for (int y = height - 2; y >= 0; y--) {
        double yNorm = (double)(height - 1 - y) / (double)(height - 1);
        double blockChance = 1.0 / (yNorm + 0.1) / b;
        for (int x = 0; x < width; x++) {
            if (rand_double() < blockChance) {
                grid[y][x] = '#';
            }
        }
    }

    // Mirror: left half copied to right half (x-symmetry)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {
            int opp = width - 1 - x;
            grid[y][opp] = grid[y][x];
        }
    }

    // Fill small air pockets (< 10 cells) with walls
    {
        std::set<Pos> visited;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (grid[y][x] != '.' || visited.count({x, y})) continue;
                auto region = flood_fill_empty(grid, width, height, x, y, visited);
                if ((int)region.size() < 10) {
                    for (auto& p : region) grid[p.y][p.x] = '#';
                }
                for (auto& p : region) visited.insert(p);
            }
        }
    }

    // Tight-space removal: iterate until stable
    // For each empty cell with 3+ wall neighbors (4-dir), find wall neighbors
    // at same row or ABOVE (n.y <= c.y), shuffle, remove one + its mirror.
    {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (grid[y][x] != '.') continue;
                    if (count_wall_neighbors_4(grid, width, height, x, y) < 3) continue;

                    // Collect wall neighbors at same row or above
                    struct Neighbor { int nx, ny; };
                    std::vector<Neighbor> candidates;
                    const int ddx[] = {0, 0, -1, 1};
                    const int ddy[] = {-1, 1, 0, 0};
                    for (int d = 0; d < 4; d++) {
                        int nx = x + ddx[d], ny = y + ddy[d];
                        if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                        if (grid[ny][nx] != '#') continue;
                        if (ny <= y) {  // Same row or above
                            candidates.push_back({nx, ny});
                        }
                    }
                    if (candidates.empty()) continue;

                    // Shuffle candidates
                    for (int i = (int)candidates.size() - 1; i > 0; i--) {
                        int j = rand() % (i + 1);
                        std::swap(candidates[i], candidates[j]);
                    }

                    // Remove first candidate + its mirror
                    auto& c = candidates[0];
                    grid[c.ny][c.nx] = '.';
                    int mx = width - 1 - c.nx;
                    if (mx != c.nx) grid[c.ny][mx] = '.';
                    changed = true;
                }
            }
        }
    }

    // Sink the lowest island (Java: find bottom-connected component,
    // count full rows from bottom in it, shift down randomly)
    {
        // Find bottom-connected wall component
        std::set<Pos> bottom_component;
        std::set<Pos> visited;
        for (int x = 0; x < width; x++) {
            if (grid[height - 1][x] == '#' && !visited.count({x, height - 1})) {
                auto region = flood_fill_walls(grid, width, height, x, height - 1, visited);
                for (auto& p : region) {
                    bottom_component.insert(p);
                    visited.insert(p);
                }
            }
        }

        // Count how many full rows from the bottom are entirely in the island
        int lowerBy = 0;
        for (int y = height - 1; y >= 0; y--) {
            // Check that every wall cell in this row belongs to the bottom component
            bool row_all_island = true;
            for (int x = 0; x < width; x++) {
                if (grid[y][x] == '#' && !bottom_component.count({x, y})) {
                    row_all_island = false;
                    break;
                }
            }
            if (row_all_island) {
                lowerBy++;
            } else {
                break;
            }
        }

        if (lowerBy >= 2) {
            int shift = 2 + rand() % (lowerBy - 1);  // random in [2, lowerBy]
            if (shift > lowerBy) shift = lowerBy;

            // Collect all island cells
            std::vector<Pos> cells(bottom_component.begin(), bottom_component.end());

            // Remove island from grid
            for (auto& p : cells) grid[p.y][p.x] = '.';

            // Re-place shifted down by 'shift' (cells going off-grid are lost)
            for (auto& p : cells) {
                int ny = p.y + shift;
                if (ny < height) {
                    grid[ny][p.x] = '#';
                }
            }
        }
    }

    // Place apples: 2.5% on left-half empty cells, mirrored
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {
            if (grid[y][x] != '.') continue;
            if (rand_double() < 0.025) {
                int mx = width - 1 - x;
                energy.push_back({x, y});
                if (mx != x) energy.push_back({mx, y});
            }
        }
    }

    // Convert lone walls (0 wall neighbors in 8-adjacency) to apples + mirror
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {
            if (grid[y][x] != '#') continue;
            if (count_wall_neighbors_8(grid, width, height, x, y) == 0) {
                int mx = width - 1 - x;
                grid[y][x] = '.';
                energy.push_back({x, y});
                if (mx != x) {
                    grid[y][mx] = '.';
                    energy.push_back({mx, y});
                }
            }
        }
    }

    // Remove apples that are on walls (safety check)
    energy.erase(std::remove_if(energy.begin(), energy.end(),
        [&](const Pos& p) { return grid[p.y][p.x] == '#'; }), energy.end());

    // Find spawn locations
    // DESIRED_SPAWNS=4, -1 if height<=15, -1 if height<=10
    int desired_spawns = 4;
    if (height <= 15) desired_spawns--;
    if (height <= 10) desired_spawns--;

    // Spawn candidates: wall cells with >= 3 free cells above
    struct SpawnCandidate { int x, y; };
    std::vector<SpawnCandidate> candidates;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (grid[y][x] != '#') continue;
            if (get_free_above(grid, x, y) >= 3) {
                candidates.push_back({x, y});
            }
        }
    }

    // Shuffle candidates
    for (int i = (int)candidates.size() - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        std::swap(candidates[i], candidates[j]);
    }

    // Select spawns: not on center columns, not 8-adjacent to existing spawns or mirrors
    std::vector<SpawnCandidate> selected_spawns;
    std::set<Pos> spawn_cells;  // All spawn cell positions (including mirrors)

    for (auto& c : candidates) {
        if ((int)selected_spawns.size() >= desired_spawns) break;

        // Not on center columns
        if (c.x == width / 2 - 1 || c.x == width / 2) continue;

        // Check 8-adjacency to existing spawn cells and their mirrors
        // Spawn occupies (x, y-1), (x, y-2), (x, y-3) and mirror at (width-1-x, same y's)
        bool too_close = false;
        // Check all 3 cells of this spawn + mirror for adjacency to existing spawns
        for (int dy = 1; dy <= 3; dy++) {
            int sy = c.y - dy;
            if (sy < 0) { too_close = true; break; }
            if (adjacent8_to_set(c.x, sy, spawn_cells)) { too_close = true; break; }
            int mx = width - 1 - c.x;
            if (adjacent8_to_set(mx, sy, spawn_cells)) { too_close = true; break; }
        }
        if (too_close) continue;

        // Also check the spawn base cell itself
        if (adjacent8_to_set(c.x, c.y, spawn_cells)) continue;
        int mx = width - 1 - c.x;
        if (adjacent8_to_set(mx, c.y, spawn_cells)) continue;

        // Verify mirror also has 3 free above
        if (get_free_above(grid, mx, c.y) < 3) continue;

        selected_spawns.push_back(c);

        // Add spawn cells + mirror to the set
        for (int dy = 0; dy <= 3; dy++) {
            spawn_cells.insert({c.x, c.y - dy});
            spawn_cells.insert({mx, c.y - dy});
        }
    }

    // Remove apples at spawn positions
    energy.erase(std::remove_if(energy.begin(), energy.end(),
        [&](const Pos& p) { return spawn_cells.count(p); }), energy.end());

    // Create snakes: player 0 on left (or as-is), player 1 mirrored
    // Distribute spawns across players: each spawn creates a pair (left + mirror)
    int snake_id = 0;
    for (auto& sp : selected_spawns) {
        int lx = sp.x;
        int rx = width - 1 - sp.x;

        // Player 0 gets the left one, player 1 gets the right
        // If spawn is already on right half, swap
        if (lx > rx) std::swap(lx, rx);

        Snakebot s0;
        s0.id = snake_id++;
        s0.owner = 0;
        s0.body = {{lx, sp.y - 3}, {lx, sp.y - 2}, {lx, sp.y - 1}};
        s0.dir = UP;
        snakes.push_back(s0);

        Snakebot s1;
        s1.id = snake_id++;
        s1.owner = 1;
        s1.body = {{rx, sp.y - 3}, {rx, sp.y - 2}, {rx, sp.y - 1}};
        s1.dir = UP;
        snakes.push_back(s1);
    }

    // Fallback: if no snakes were created, force a default setup
    if (snakes.empty()) {
        // Clear space and place on bottom platform
        for (int x = 2; x < width / 2; x++) {
            if (grid[height - 1][x] == '#' && height >= 4) {
                int mx = width - 1 - x;
                for (int dy = 1; dy <= 3; dy++) {
                    grid[height - 1 - dy][x] = '.';
                    grid[height - 1 - dy][mx] = '.';
                }
                Snakebot s0;
                s0.id = 0; s0.owner = 0; s0.dir = UP;
                s0.body = {{x, height - 4}, {x, height - 3}, {x, height - 2}};
                snakes.push_back(s0);

                Snakebot s1;
                s1.id = 1; s1.owner = 1; s1.dir = UP;
                s1.body = {{mx, height - 4}, {mx, height - 3}, {mx, height - 2}};
                snakes.push_back(s1);
                break;
            }
        }
    }

    // Remove any energy that overlaps with snake positions
    std::set<Pos> snake_pos;
    for (auto& s : snakes)
        for (auto& bp : s.body)
            snake_pos.insert(bp);
    energy.erase(std::remove_if(energy.begin(), energy.end(),
        [&](const Pos& p) { return snake_pos.count(p); }), energy.end());
}

// ===== Protocol I/O =====

std::string GameState::build_init_input(int player_id) const {
    std::ostringstream oss;
    oss << player_id << "\n";
    oss << width << "\n";
    oss << height << "\n";

    for (int y = 0; y < height; y++) {
        oss << grid[y] << "\n";
    }

    auto my_ids = get_player_snake_ids(player_id);
    auto opp_ids = get_player_snake_ids(1 - player_id);

    oss << (int)my_ids.size() << "\n";
    for (int id : my_ids) oss << id << "\n";
    for (int id : opp_ids) oss << id << "\n";

    return oss.str();
}

std::string GameState::build_turn_input() const {
    std::ostringstream oss;

    // Energy
    oss << (int)energy.size() << "\n";
    for (auto& e : energy) {
        oss << e.x << " " << e.y << "\n";
    }

    // Alive snakes
    int alive_count = 0;
    for (auto& s : snakes) if (s.alive) alive_count++;
    oss << alive_count << "\n";

    for (auto& s : snakes) {
        if (!s.alive) continue;
        oss << s.id << " ";
        for (int i = 0; i < (int)s.body.size(); i++) {
            if (i > 0) oss << ":";
            oss << s.body[i].x << "," << s.body[i].y;
        }
        oss << "\n";
    }

    return oss.str();
}

bool GameState::parse_actions(int player_id, const std::string& action_str) {
    std::vector<std::string> actions;
    std::istringstream iss(action_str);
    std::string token;
    while (std::getline(iss, token, ';')) {
        size_t start = token.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = token.find_last_not_of(" \t\r\n");
        actions.push_back(token.substr(start, end - start + 1));
    }

    for (auto& act : actions) {
        if (act == "WAIT") continue;
        if (act.substr(0, 4) == "MARK") continue;

        std::istringstream as(act);
        int snake_id;
        std::string direction;
        if (!(as >> snake_id >> direction)) {
            std::cerr << "Invalid action: " << act << std::endl;
            return false;
        }

        std::string debug_text;
        std::getline(as, debug_text);
        if (!debug_text.empty() && debug_text[0] == ' ')
            debug_text = debug_text.substr(1);

        Snakebot* snake = get_snake(snake_id);
        if (!snake || !snake->alive) {
            std::cerr << "Snake " << snake_id << " not found or dead" << std::endl;
            continue;
        }
        if (snake->owner != player_id) {
            std::cerr << "Player " << player_id << " doesn't own snake " << snake_id << std::endl;
            return false;
        }

        if (direction == "UP") snake->dir = UP;
        else if (direction == "DOWN") snake->dir = DOWN;
        else if (direction == "LEFT") snake->dir = LEFT;
        else if (direction == "RIGHT") snake->dir = RIGHT;
        else {
            std::cerr << "Invalid direction: " << direction << std::endl;
            return false;
        }

        snake->debug_text = debug_text;
    }

    return true;
}

// ===== Game step — separate phases matching Java Game.java =====

void GameState::step() {
    turn++;
    do_moves_and_eats();
    do_beheadings();
    apply_gravity();
    check_game_over();
}

// Phase 1+2: Move and eat — extend head, only remove tail if not eating
void GameState::do_moves_and_eats() {
    std::set<int> eaten_indices;

    for (auto& s : snakes) {
        if (!s.alive) continue;
        Pos new_head = s.head() + dir_delta(s.dir);
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

    // Remove eaten energy (reverse order to maintain indices)
    std::vector<int> to_remove(eaten_indices.begin(), eaten_indices.end());
    std::sort(to_remove.rbegin(), to_remove.rend());
    for (int i : to_remove) {
        energy.erase(energy.begin() + i);
    }
}

// Phase 3: Beheading — head in wall or in any body (own non-head or other snake's body)
void GameState::do_beheadings() {
    std::set<int> to_behead;  // snake IDs to behead

    for (auto& s : snakes) {
        if (!s.alive) continue;
        Pos head = s.head();

        // Check if head is in a wall
        if (!in_bounds(head) || is_platform(head)) {
            to_behead.insert(s.id);
            continue;
        }

        // Check if head is in any body (own non-head, or any other snake's body)
        bool collides = false;
        for (auto& other : snakes) {
            if (!other.alive) continue;
            int start_idx = (other.id == s.id) ? 1 : 0;  // Skip own head
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

    // Apply beheadings
    for (int id : to_behead) {
        Snakebot* s = get_snake(id);
        if (!s) continue;
        if (s->length() > 3) {
            s->body.erase(s->body.begin());  // Remove head
        } else {
            s->alive = false;
        }
    }
}

// Phase 4: Gravity with intercoiled snake groups
void GameState::apply_gravity() {
    bool something_fell = true;
    while (something_fell) {
        something_fell = false;

        // Grounded propagation: iteratively find snakes supported by
        // platform, energy, or an already-grounded snake below them
        std::set<int> airborne_ids;
        for (auto& s : snakes) {
            if (s.alive) airborne_ids.insert(s.id);
        }
        std::set<int> grounded_ids;

        bool something_got_grounded = true;
        while (something_got_grounded) {
            something_got_grounded = false;
            for (auto& s : snakes) {
                if (!s.alive || !airborne_ids.count(s.id)) continue;
                bool grounded = false;
                for (auto& bp : s.body) {
                    Pos below = {bp.x, bp.y + 1};
                    if (is_platform(below)) { grounded = true; break; }
                    if (has_energy(below)) { grounded = true; break; }
                    // Supported by a grounded snake's body
                    for (auto& other : snakes) {
                        if (!other.alive || !grounded_ids.count(other.id)) continue;
                        for (auto& obp : other.body) {
                            if (obp == below) { grounded = true; break; }
                        }
                        if (grounded) break;
                    }
                    if (grounded) break;
                }
                if (grounded) {
                    grounded_ids.insert(s.id);
                    airborne_ids.erase(s.id);
                    something_got_grounded = true;
                }
            }
        }

        // Drop all airborne snakes by 1
        for (auto& s : snakes) {
            if (!s.alive || !airborne_ids.count(s.id)) continue;
            for (auto& bp : s.body) {
                bp.y += 1;
            }
            something_fell = true;
        }

        // Kill snakes that fell off the grid
        for (auto& s : snakes) {
            if (!s.alive) continue;
            for (auto& bp : s.body) {
                if (!in_bounds(bp)) {
                    s.alive = false;
                    something_fell = true;
                    break;
                }
            }
        }
    }
}

void GameState::check_game_over() {
    bool p0_alive = !get_alive_snake_ids(0).empty();
    bool p1_alive = !get_alive_snake_ids(1).empty();

    if (!p0_alive || !p1_alive) {
        game_over = true;
    }

    if (turn >= max_turns) {
        game_over = true;
    }

    if (game_over) {
        int score0 = count_body_parts(0);
        int score1 = count_body_parts(1);
        if (score0 > score1) winner = 0;
        else if (score1 > score0) winner = 1;
        else winner = -1;
    }
}

// ===== JSON output =====

static std::string escape_json_str(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string GameState::frame_json() const {
    std::ostringstream o;
    o << "{\"turn\":" << turn;

    o << ",\"energy\":[";
    for (int i = 0; i < (int)energy.size(); i++) {
        if (i > 0) o << ",";
        o << "[" << energy[i].x << "," << energy[i].y << "]";
    }
    o << "]";

    o << ",\"snakes\":[";
    bool first = true;
    for (auto& s : snakes) {
        if (!first) o << ",";
        first = false;
        o << "{\"id\":" << s.id
          << ",\"owner\":" << s.owner
          << ",\"alive\":" << (s.alive ? "true" : "false")
          << ",\"dir\":\"" << dir_name(s.dir) << "\"";
        if (!s.debug_text.empty()) {
            o << ",\"debug\":\"" << escape_json_str(s.debug_text) << "\"";
        }
        o << ",\"body\":[";
        for (int i = 0; i < (int)s.body.size(); i++) {
            if (i > 0) o << ",";
            o << "[" << s.body[i].x << "," << s.body[i].y << "]";
        }
        o << "]}";
    }
    o << "]";

    o << ",\"scores\":[" << count_body_parts(0) << "," << count_body_parts(1) << "]";
    o << ",\"game_over\":" << (game_over ? "true" : "false");
    o << "}";
    return o.str();
}

std::string GameState::game_json_header() const {
    std::ostringstream o;
    o << "\"width\":" << width
      << ",\"height\":" << height
      << ",\"grid\":[";
    for (int y = 0; y < height; y++) {
        if (y > 0) o << ",";
        o << "\"" << grid[y] << "\"";
    }
    o << "]";
    return o.str();
}
