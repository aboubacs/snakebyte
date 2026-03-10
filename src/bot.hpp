#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>

#include "sim.hpp"

class Bot {
public:
    void init();
    void read_turn();
    void think();

    int depth = 10;  // configurable rollout depth

private:
    int my_id_ = 0;
    int turn_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> grid_;

    std::vector<int> my_snake_ids_;
    std::vector<int> opp_snake_ids_;

    // Current game state rebuilt each turn
    SimState state_;

    // Best move sequence from previous turn (shifted by 1)
    // Outer: turn index in sequence, Inner: one SimDir per alive snake
    std::vector<std::vector<SimDir>> prev_best_seq_;

    // Build a SimState from current parsed data
    SimState build_state() const;

    // Generate a random move sequence of given length for our snakes
    std::vector<std::vector<SimDir>> random_sequence(int len, int num_snakes,
                                                      const std::vector<int>& alive_ids) const;

    // Simulate a sequence and return eval score
    int simulate(const SimState& base, const std::vector<int>& alive_ids,
                 const std::vector<std::vector<SimDir>>& seq) const;
};
