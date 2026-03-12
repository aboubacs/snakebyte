#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cmath>

#include "../sim.hpp"

// An individual is a sequence of moves: [step][snake]
using Individual = std::vector<std::vector<SimDir>>;

struct ScoredIndividual {
    Individual seq;
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
    int flex_count = 5;  // number of random opp sequences to test against

private:
    int my_id_ = 0;
    int turn_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::string> grid_;

    std::vector<int> my_snake_ids_;
    std::vector<int> opp_snake_ids_;

    SimState state_;

    // Best individual from previous turn
    Individual prev_best_;

    std::vector<SimDir> get_initial_dirs(const std::vector<int>& alive_ids) const;

    Individual random_individual(int len, int num_snakes,
                                 const std::vector<SimDir>& initial_dirs) const;

    void repair(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    Individual crossover_time_split(const Individual& a, const Individual& b,
                                     const std::vector<SimDir>& initial_dirs) const;
    Individual crossover_snake_split(const Individual& a, const Individual& b,
                                      int num_snakes,
                                      const std::vector<SimDir>& initial_dirs) const;

    void mutate(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    // Evaluate with opponent holding direction (used during GA phase)
    double evaluate(const SimState& base, const std::vector<int>& alive_ids,
                    const Individual& ind) const;

    // Evaluate against specific opponent moves (used during flex phase)
    double evaluate_vs(const SimState& base, const std::vector<int>& alive_ids,
                       const Individual& ind,
                       const std::vector<int>& opp_alive_ids,
                       const Individual& opp_ind) const;

    int select_parent(const std::vector<ScoredIndividual>& pop) const;
};
