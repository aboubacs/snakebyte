#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cassert>

struct Pos {
    int x, y;
    bool operator==(const Pos& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const { return x < o.x || (x == o.x && y < o.y); }
    Pos operator+(const Pos& o) const { return {x + o.x, y + o.y}; }
};

enum Direction { UP, DOWN, LEFT, RIGHT };

inline Pos dir_delta(Direction d) {
    switch (d) {
        case UP:    return {0, -1};
        case DOWN:  return {0, 1};
        case LEFT:  return {-1, 0};
        case RIGHT: return {1, 0};
    }
    return {0, 0};
}

inline std::string dir_name(Direction d) {
    switch (d) {
        case UP:    return "UP";
        case DOWN:  return "DOWN";
        case LEFT:  return "LEFT";
        case RIGHT: return "RIGHT";
    }
    return "UP";
}

struct Snakebot {
    int id;
    int owner;  // player index (0 or 1)
    std::vector<Pos> body;  // body[0] = head
    Direction dir = UP;
    bool alive = true;
    std::string debug_text;
    Pos head() const { return body[0]; }
    int length() const { return (int)body.size(); }
};

struct GameState {
    int width = 0;
    int height = 0;
    int max_turns = 200;
    int turn = 0;
    int num_players = 2;
    bool game_over = false;
    int winner = -1;  // -1 = draw

    std::vector<std::string> grid;  // height rows of width chars: '#' or '.'
    std::vector<Pos> energy;
    std::vector<Snakebot> snakes;

    // Generate a map for testing
    void init_default_map();

    // Check if position is in bounds
    bool in_bounds(Pos p) const {
        return p.x >= 0 && p.x < width && p.y >= 0 && p.y < height;
    }

    // Check if position is a platform
    bool is_platform(Pos p) const {
        if (!in_bounds(p)) return false;
        return grid[p.y][p.x] == '#';
    }

    // Check if position has energy
    bool has_energy(Pos p) const {
        for (auto& e : energy) if (e == p) return true;
        return false;
    }

    // Check if a position is occupied by any alive snake body
    bool is_snake_body(Pos p, int exclude_snake_id = -1) const {
        for (auto& s : snakes) {
            if (!s.alive || s.id == exclude_snake_id) continue;
            for (auto& bp : s.body) {
                if (bp == p) return true;
            }
        }
        return false;
    }

    // Check if position is solid (platform, energy, or snake body)
    bool is_solid(Pos p, int exclude_snake_id = -1) const {
        if (is_platform(p)) return true;
        if (has_energy(p)) return true;
        if (is_snake_body(p, exclude_snake_id)) return true;
        return false;
    }

    // Get snake IDs owned by a player
    std::vector<int> get_player_snake_ids(int player) const {
        std::vector<int> ids;
        for (auto& s : snakes) {
            if (s.owner == player) ids.push_back(s.id);
        }
        return ids;
    }

    // Get all alive snake IDs for a player
    std::vector<int> get_alive_snake_ids(int player) const {
        std::vector<int> ids;
        for (auto& s : snakes) {
            if (s.owner == player && s.alive) ids.push_back(s.id);
        }
        return ids;
    }

    // Get snake by ID
    Snakebot* get_snake(int id) {
        for (auto& s : snakes) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }

    // Count total body parts for a player
    int count_body_parts(int player) const {
        int total = 0;
        for (auto& s : snakes) {
            if (s.owner == player && s.alive) {
                total += s.length();
            }
        }
        return total;
    }

    // Build init input for a player
    std::string build_init_input(int player_id) const;

    // Build turn input for a player
    std::string build_turn_input() const;

    // Parse player action string, apply directions
    bool parse_actions(int player_id, const std::string& action_str);

    // Execute one full game turn (after directions are set)
    void step();

    // Check end conditions
    void check_game_over();

    // JSON for one frame
    std::string frame_json() const;

    // Full game log JSON header
    std::string game_json_header() const;

private:
    // Game phases (called by step in order)
    void do_moves_and_eats();
    void do_beheadings();

    // Apply gravity with intercoiled group logic
    void apply_gravity();
};
