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
    int energy_k = 3;
    bool eval_decay = false;
    int cheat_factor = 1;
    double center_control_factor = 0.0;
    int flex_count = 5;  // number of random opp sequences for flex re-ranking

    // Opponent simulation parameters
    int opp_depth = 6;
    double opp_time_pct = 0.4;  // 40% of time for opponent simulation

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

    // Predicted opponent moves from last think()
    Individual opp_moves_;

    // Get initial directions for alive snakes
    std::vector<SimDir> get_initial_dirs(const std::vector<int>& alive_ids) const;

    // Generate a random individual respecting no-reverse
    Individual random_individual(int len, int num_snakes,
                                 const std::vector<SimDir>& initial_dirs) const;

    // Repair reverse moves after crossover
    void repair(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    // Crossover operators
    Individual crossover_time_split(const Individual& a, const Individual& b,
                                     const std::vector<SimDir>& initial_dirs) const;
    Individual crossover_snake_split(const Individual& a, const Individual& b,
                                      int num_snakes,
                                      const std::vector<SimDir>& initial_dirs) const;

    // Mutate an individual in place
    void mutate(Individual& ind, const std::vector<SimDir>& initial_dirs) const;

    // Evaluate an individual (uses opp_moves_ for opponent simulation)
    double evaluate(const SimState& base, const std::vector<int>& alive_ids,
                    const Individual& ind,
                    const std::vector<int>& opp_alive_ids,
                    const Individual& opp_ind) const;

    // Evaluate for opponent (flipped perspective)
    double evaluate_opp(const SimState& base, const std::vector<int>& opp_alive_ids,
                        const Individual& opp_ind,
                        const std::vector<int>& my_alive_ids) const;

    // Select a parent via fitness-proportional selection
    int select_parent(const std::vector<ScoredIndividual>& pop) const;

    // Run GA for a set of snake IDs with a given deadline and eval function
    std::vector<ScoredIndividual> run_ga(const std::vector<int>& alive_ids,
                      const std::vector<SimDir>& initial_dirs,
                      int ga_depth, Individual* seed,
                      std::chrono::steady_clock::time_point deadline,
                      bool for_opponent,
                      const std::vector<int>& other_alive_ids,
                      const Individual& other_moves,
                      int& generations_out);
};
