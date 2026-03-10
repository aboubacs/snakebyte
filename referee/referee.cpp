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

static int rand_range(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

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

// ===== Map generation =====

void GameState::init_default_map() {
    srand(time(nullptr) ^ getpid());

    // Height 10-24, width = round(height * 1.8), made even
    height = rand_range(10, 24);
    width = (int)round(height * 1.8);
    if (width % 2 != 0) width++;

    grid.assign(height, std::string(width, '.'));

    // Bottom row is all walls
    grid[height - 1] = std::string(width, '#');

    // Probabilistic wall placement, row by row from bottom-1 up
    double b = 5.0 + rand_double() * 10.0;
    for (int y = height - 2; y >= 0; y--) {
        double yNorm = (double)(height - 1 - y) / (double)height;
        double blockChance = 1.0 / (yNorm + 0.1) / b;
        // Only place on left half, mirror to right
        for (int x = 0; x < width / 2; x++) {
            if (rand_double() < blockChance) {
                grid[y][x] = '#';
                grid[y][width - 1 - x] = '#';
            }
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

    // Open up tight corridors: for each empty cell with 3+ wall neighbors (4-dir),
    // remove one wall neighbor to widen the passage. Single pass, preserving symmetry.
    {
        for (int y = 1; y < height - 1; y++) {
            for (int x = 0; x < width / 2; x++) {
                if (grid[y][x] != '.') continue;
                if (count_wall_neighbors_4(grid, width, height, x, y) >= 3) {
                    // Find a wall neighbor to remove (prefer up)
                    const int dx[] = {0, 0, -1, 1};
                    const int dy[] = {-1, 1, 0, 0};
                    for (int d = 0; d < 4; d++) {
                        int nx = x + dx[d], ny = y + dy[d];
                        if (ny == height - 1) continue;  // Don't remove bottom row
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height && grid[ny][nx] == '#') {
                            grid[ny][nx] = '.';
                            grid[ny][width - 1 - nx] = '.';
                            break;
                        }
                    }
                }
            }
        }
    }

    // Find and sink floating wall islands (not connected to bottom row)
    {
        // Find the main ground-connected wall component
        std::set<Pos> ground_walls;
        std::set<Pos> visited;
        for (int x = 0; x < width; x++) {
            if (grid[height - 1][x] == '#' && !visited.count({x, height - 1})) {
                auto region = flood_fill_walls(grid, width, height, x, height - 1, visited);
                for (auto& p : region) {
                    ground_walls.insert(p);
                    visited.insert(p);
                }
            }
        }

        // Find floating islands and sink them (move down until they connect or hit bottom)
        visited.clear();
        for (auto& p : ground_walls) visited.insert(p);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (grid[y][x] != '#' || visited.count({x, y})) continue;
                auto island = flood_fill_walls(grid, width, height, x, y, visited);
                for (auto& p : island) visited.insert(p);

                if (island.empty()) continue;

                // Remove island from grid
                for (auto& p : island) grid[p.y][p.x] = '.';

                // Find how far down to move
                int max_drop = height;
                for (auto& p : island) {
                    int drop = 0;
                    for (int ny = p.y + 1; ny < height; ny++) {
                        bool blocked = false;
                        // Check if this position is occupied by ground wall or other island cell
                        if (ground_walls.count({p.x, ny})) { blocked = true; }
                        if (blocked) break;
                        drop++;
                    }
                    max_drop = std::min(max_drop, drop);
                }

                // Place island at new position
                if (max_drop > 0) {
                    for (auto& p : island) {
                        int ny = p.y + max_drop;
                        if (ny < height) {
                            grid[ny][p.x] = '#';
                        }
                    }
                } else {
                    // Can't sink, put back
                    for (auto& p : island) grid[p.y][p.x] = '#';
                }
            }
        }
    }

    // Place apples on empty cells above platforms (on left half, mirrored)
    // Target: roughly 5-10% of platform-top cells
    {
        std::vector<Pos> valid_spots;
        for (int y = 0; y < height - 1; y++) {
            for (int x = 0; x < width / 2; x++) {
                if (grid[y][x] != '.') continue;
                // Prefer cells above a platform
                if (y + 1 < height && grid[y + 1][x] == '#') {
                    valid_spots.push_back({x, y});
                }
            }
        }
        // Also add some floating energy on open cells
        for (int y = 0; y < height - 1; y++) {
            for (int x = 0; x < width / 2; x++) {
                if (grid[y][x] != '.') continue;
                if (rand_double() < 0.015) {
                    int mx = width - 1 - x;
                    energy.push_back({x, y});
                    if (mx != x) energy.push_back({mx, y});
                }
            }
        }
        // Place on valid platform-top spots
        for (auto& p : valid_spots) {
            if (rand_double() < 0.15) {
                int mx = width - 1 - p.x;
                energy.push_back({p.x, p.y});
                if (mx != p.x) energy.push_back({mx, p.y});
            }
        }
    }

    // Convert lone walls (0 wall neighbors in 8-adjacency, not bottom row) to apples
    for (int y = 0; y < height - 1; y++) {
        for (int x = 0; x < width; x++) {
            if (grid[y][x] != '#') continue;
            if (count_wall_neighbors_8(grid, width, height, x, y) == 0) {
                grid[y][x] = '.';
                energy.push_back({x, y});
            }
        }
    }

    // Remove apples that are on walls (safety check)
    energy.erase(std::remove_if(energy.begin(), energy.end(),
        [&](const Pos& p) { return grid[p.y][p.x] == '#'; }), energy.end());

    // Find spawn locations: wall cells with 3 free cells above
    struct SpawnCandidate {
        int x, y;  // y = wall row, snake goes at y-1, y-2, y-3
    };
    std::vector<SpawnCandidate> candidates;

    auto is_free = [&](int x, int y) -> bool {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        if (grid[y][x] != '.') return false;
        // Also check no energy at this pos
        for (auto& e : energy) if (e.x == x && e.y == y) return false;
        return true;
    };

    for (int y = 1; y < height; y++) {
        for (int x = 0; x < width / 2; x++) {
            if (grid[y][x] != '#') continue;
            // Need 3 free cells above
            if (y - 3 < 0) continue;
            if (!is_free(x, y - 1) || !is_free(x, y - 2) || !is_free(x, y - 3)) continue;
            // Not too close to center
            if (abs(x - width / 2) < 3) continue;
            candidates.push_back({x, y});
        }
    }

    // Determine number of snakes per player based on height
    int snakes_per_player = (height >= 16 && candidates.size() >= 2) ? 2 : 1;

    // Select spawn positions, ensuring minimum distance between them
    std::vector<SpawnCandidate> selected_spawns;
    int min_dist = std::max(3, height / 4);

    // Shuffle candidates
    for (int i = (int)candidates.size() - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        std::swap(candidates[i], candidates[j]);
    }

    for (auto& c : candidates) {
        if ((int)selected_spawns.size() >= snakes_per_player) break;
        bool too_close = false;
        for (auto& s : selected_spawns) {
            if (abs(c.x - s.x) + abs(c.y - s.y) < min_dist) {
                too_close = true;
                break;
            }
        }
        if (!too_close) {
            selected_spawns.push_back(c);
        }
    }

    // Fallback: if no valid spawns found, use bottom platform
    if (selected_spawns.empty()) {
        // Find any wall with space above
        for (int x = 2; x < width / 2; x++) {
            if (grid[height - 1][x] == '#' && height >= 4) {
                // Clear 3 cells above
                for (int dy = 1; dy <= 3; dy++) {
                    grid[height - 1 - dy][x] = '.';
                    grid[height - 1 - dy][width - 1 - x] = '.';
                }
                selected_spawns.push_back({x, height - 1});
                break;
            }
        }
    }

    // Create snakes: player 0 on left, player 1 mirrored on right
    int snake_id = 0;
    for (auto& sp : selected_spawns) {
        // Player 0 snake (left side)
        Snakebot s0;
        s0.id = snake_id++;
        s0.owner = 0;
        // Head at top (y-3), body going down: body[0]=head
        s0.body = {{sp.x, sp.y - 3}, {sp.x, sp.y - 2}, {sp.x, sp.y - 1}};
        s0.dir = UP;
        snakes.push_back(s0);

        // Player 1 snake (mirrored)
        Snakebot s1;
        s1.id = snake_id++;
        s1.owner = 1;
        int mx = width - 1 - sp.x;
        s1.body = {{mx, sp.y - 3}, {mx, sp.y - 2}, {mx, sp.y - 1}};
        s1.dir = UP;
        snakes.push_back(s1);
    }

    // If somehow no snakes were created, force a default setup
    if (snakes.empty()) {
        Snakebot s0;
        s0.id = 0; s0.owner = 0; s0.dir = UP;
        s0.body = {{2, height - 4}, {2, height - 3}, {2, height - 2}};
        snakes.push_back(s0);

        Snakebot s1;
        s1.id = 1; s1.owner = 1; s1.dir = UP;
        s1.body = {{width - 3, height - 4}, {width - 3, height - 3}, {width - 3, height - 2}};
        snakes.push_back(s1);
    }

    // Remove any energy that overlaps with snake positions
    std::set<Pos> snake_cells;
    for (auto& s : snakes)
        for (auto& bp : s.body)
            snake_cells.insert(bp);
    energy.erase(std::remove_if(energy.begin(), energy.end(),
        [&](const Pos& p) { return snake_cells.count(p); }), energy.end());
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
    do_moves();
    do_eats();
    do_beheadings();
    apply_gravity();
    check_game_over();
}

// Phase 1: All snakes move (extend head in direction, remove tail)
void GameState::do_moves() {
    for (auto& s : snakes) {
        if (!s.alive) continue;
        Pos new_head = s.head() + dir_delta(s.dir);
        s.body.insert(s.body.begin(), new_head);
        s.body.pop_back();  // Always remove tail in move phase
    }
}

// Phase 2: Check eating — if head is on energy, grow (re-add tail segment)
void GameState::do_eats() {
    std::set<int> eaten_indices;

    for (auto& s : snakes) {
        if (!s.alive) continue;
        Pos head = s.head();

        for (int i = 0; i < (int)energy.size(); i++) {
            if (energy[i] == head && !eaten_indices.count(i)) {
                // Snake eats: grow by duplicating tail
                s.body.push_back(s.body.back());
                eaten_indices.insert(i);
                break;
            }
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
        if (s->length() >= 3) {
            s->body.erase(s->body.begin());  // Remove head
        } else {
            s->alive = false;
        }
    }
}

// Phase 4: Gravity with intercoiled snake groups
void GameState::apply_gravity() {
    bool changed = true;
    while (changed) {
        changed = false;

        // Build adjacency-based groups using union-find
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

        // Check adjacency between all pairs of alive snakes
        for (int i = 0; i < (int)snakes.size(); i++) {
            if (!snakes[i].alive) continue;
            for (int j = i + 1; j < (int)snakes.size(); j++) {
                if (!snakes[j].alive) continue;
                bool adjacent = false;
                for (auto& bp1 : snakes[i].body) {
                    for (auto& bp2 : snakes[j].body) {
                        int dist = abs(bp1.x - bp2.x) + abs(bp1.y - bp2.y);
                        if (dist <= 1) {  // Adjacent or overlapping
                            adjacent = true;
                            break;
                        }
                    }
                    if (adjacent) break;
                }
                if (adjacent) unite(snakes[i].id, snakes[j].id);
            }
        }

        // Collect group members
        std::map<int, std::vector<int>> groups;  // group_id -> snake indices
        for (int i = 0; i < (int)snakes.size(); i++) {
            if (!snakes[i].alive) continue;
            groups[uf_find(snakes[i].id)].push_back(i);
        }

        // Collect all body positions per group
        std::map<int, std::set<Pos>> group_cells;
        for (auto& [gid, indices] : groups) {
            for (int idx : indices) {
                for (auto& bp : snakes[idx].body) {
                    group_cells[gid].insert(bp);
                }
            }
        }

        // Check support for each group
        for (auto& [gid, indices] : groups) {
            bool supported = false;
            for (int idx : indices) {
                if (supported) break;
                for (auto& bp : snakes[idx].body) {
                    Pos below = {bp.x, bp.y + 1};
                    // Supported by platform
                    if (is_platform(below)) { supported = true; break; }
                    // Supported by energy
                    if (has_energy(below)) { supported = true; break; }
                    // Supported by snake from a different group
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
                // Move all snakes in group down by 1
                for (int idx : indices) {
                    for (auto& bp : snakes[idx].body) {
                        bp.y += 1;
                    }
                }
                changed = true;
            }
        }

        // Kill snakes that fell off the grid
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
