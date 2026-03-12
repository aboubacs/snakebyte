#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cmath>

#include "../sim.hpp"

struct SmitsiNode {
    SimDir dir;
    int children[3];  // indices into tree's node vector, -1 = not created
    int parent;
    int visits;
    double total_score;
};

struct SmitsiTree {
    std::vector<SmitsiNode> nodes;
    int snake_id;
    int owner;       // player id
    int path[32];    // node indices selected at each depth
    int path_len;
    double low_score, high_score;

    void init(SimDir initial_dir, int sid, int own);
    void expand(int idx);
    int select(int parent_idx, double C, int rand_thresh);
    void backpropagate(double score);
    SimDir best_move() const;
};

class Bot {
public:
    void init();
    void read_turn();
    void think();

    int depth = 10;
    double explore_c = 1.41;
    int random_threshold = 10;
    bool eval_decay = false;
    int cheat_factor = 1;
    double center_control_factor = 0.0;

private:
    int my_id_ = 0;
    int turn_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> grid_;
    std::vector<int> my_snake_ids_;
    std::vector<int> opp_snake_ids_;
    SimState state_;

    double compute_eval(const SimState& sim, int player) const;
};
