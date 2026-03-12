#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cmath>

#include "../sim.hpp"

// An individual is a sequence of moves for a SINGLE snake: [step]
using DIndividual = std::vector<SimDir>;

struct DScoredIndividual {
    DIndividual seq;
    double score;
};

class Bot {
public:
    void init();
    void read_turn();
    void think();

    int depth = 10;
    int pop_size = 50;
    double mutation_rate = 0.15;
    bool cumulative_eval = true;
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

    // Best moves per snake from previous turn: [snake_index][step]
    std::vector<DIndividual> prev_best_;

    // Generate a random individual for one snake
    DIndividual random_individual(int len, SimDir initial_dir) const;

    // Repair reverse moves
    void repair(DIndividual& ind, SimDir initial_dir) const;

    // Crossover
    DIndividual crossover(const DIndividual& a, const DIndividual& b,
                          SimDir initial_dir) const;

    // Mutate
    void mutate(DIndividual& ind, SimDir initial_dir) const;

    // Evaluate a single snake's moves given fixed moves for all others
    double evaluate(const SimState& base,
                    const std::vector<int>& alive_ids,
                    int snake_idx,
                    const DIndividual& ind,
                    const std::vector<DIndividual>& fixed_moves) const;

    // Fitness-proportional parent selection
    int select_parent(const std::vector<DScoredIndividual>& pop) const;
};
