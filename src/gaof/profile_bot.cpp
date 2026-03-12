// Profiling benchmark for gaof bot simulation.
// Measures time spent in each component of the hot path.

#include "../sim.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdio>

using Clock = std::chrono::steady_clock;
using us = std::chrono::microseconds;
using ns = std::chrono::nanoseconds;

SimState make_state() {
    SimState state;
    state.width = 30;
    state.height = 18;
    state.grid.resize(state.height, std::string(state.width, '.'));
    state.grid[state.height - 1] = std::string(state.width, '#');
    for (int x = 5; x < 10; x++) state.grid[12][x] = '#';
    for (int x = 20; x < 25; x++) state.grid[12][x] = '#';
    for (int x = 10; x < 20; x++) state.grid[8][x] = '#';

    for (int i = 0; i < 20; i++) {
        state.energy.push_back({rand() % state.width, rand() % (state.height - 2)});
    }

    int id = 0;
    auto add_snake = [&](int owner, int x, int y, int len) {
        SimSnake s;
        s.id = id++;
        s.owner = owner;
        s.alive = true;
        s.dir = SIM_UP;
        for (int i = 0; i < len; i++) s.body.push_back({x, y + i});
        state.snakes.push_back(s);
    };
    add_snake(0, 7, 9, 5);
    add_snake(0, 12, 5, 3);
    add_snake(1, 22, 9, 5);
    add_snake(1, 17, 5, 3);

    return state;
}

template<typename F>
long bench(int n, F&& fn) {
    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) fn();
    return std::chrono::duration_cast<ns>(Clock::now() - t0).count();
}

int main() {
    srand(42);
    SimState state = make_state();

    int N = 50000;

    fprintf(stderr, "=== COMPONENT BENCHMARKS (x%d) ===\n\n", N);

    // --- Heuristics ---
    fprintf(stderr, "--- Heuristics (per call) ---\n");
    auto t_eval = bench(N, [&]{ state.eval(0); });
    fprintf(stderr, "  eval()             %6.1f ns\n", (double)t_eval / N);

    auto t_eprox = bench(N, [&]{ state.energy_proximity(0); });
    fprintf(stderr, "  energy_proximity() %6.1f ns\n", (double)t_eprox / N);

    auto t_height = bench(N, [&]{ state.height_advantage(0); });
    fprintf(stderr, "  height_advantage() %6.1f ns\n", (double)t_height / N);

    auto t_terr = bench(N, [&]{ state.territory(0); });
    fprintf(stderr, "  territory()        %6.1f ns\n", (double)t_terr / N);

    auto t_cc = bench(N, [&]{ state.center_control(0); });
    fprintf(stderr, "  center_control()   %6.1f ns\n", (double)t_cc / N);

    double heuristic_total_ns = (double)(t_eval + t_eprox + t_height + t_terr + t_cc) / N;
    // In evaluate(), heuristics called for both players per step:
    // eval(me), energy_proximity(me), energy_proximity(opp), height(me), height(opp), territory(me), cc(me), cc(opp)
    double heuristic_per_step = (double)(t_eval + 2*t_eprox + 2*t_height + t_terr + 2*t_cc) / N;
    fprintf(stderr, "  => heuristics/step %6.1f ns (both players)\n", heuristic_per_step);
    fprintf(stderr, "  => heuristics x10  %6.1f us (full depth)\n", heuristic_per_step * 10 / 1000);

    // --- SimState copy ---
    fprintf(stderr, "\n--- State copy ---\n");
    auto t_copy = bench(N, [&]{ SimState copy = state; (void)copy; });
    fprintf(stderr, "  SimState copy      %6.1f ns\n", (double)t_copy / N);

    // --- step() variants ---
    fprintf(stderr, "\n--- step() (per call) ---\n");

    auto t_step = bench(N, [&]{
        SimState copy = state;
        for (auto& s : copy.snakes) if (s.alive) s.dir = static_cast<SimDir>(rand() % 4);
        copy.step();
    });
    fprintf(stderr, "  step(normal)       %6.1f ns\n", (double)t_step / N);

    // Grounded: snakes on bottom
    SimState groundState = state;
    for (auto& s : groundState.snakes) {
        for (int i = 0; i < (int)s.body.size(); i++) s.body[i].y = 16 - i;
    }
    auto t_grounded = bench(N, [&]{
        SimState copy = groundState;
        for (auto& s : copy.snakes) if (s.alive) s.dir = SIM_LEFT;
        copy.step();
    });
    fprintf(stderr, "  step(grounded)     %6.1f ns\n", (double)t_grounded / N);

    // Airborne: snakes floating
    SimState airState = state;
    for (auto& s : airState.snakes) {
        for (auto& bp : s.body) bp.y = 2 + (&bp - &s.body[0]);
    }
    auto t_air = bench(N/5, [&]{
        SimState copy = airState;
        for (auto& s : copy.snakes) if (s.alive) s.dir = static_cast<SimDir>(rand() % 4);
        copy.step();
    });
    fprintf(stderr, "  step(airborne)     %6.1f ns  (falls ~14 cells)\n", (double)t_air / (N/5));

    // Gravity cost = step(normal) - step(grounded)
    double gravity_cost = (double)t_step / N - (double)t_grounded / N;
    fprintf(stderr, "  => gravity overhead %5.1f ns (normal - grounded)\n", gravity_cost);

    // --- Full evaluate simulation ---
    fprintf(stderr, "\n--- Full evaluate (depth=10, 4 snakes) ---\n");
    int depth = 10;
    std::vector<std::vector<SimDir>> my_ind(depth), opp_ind(depth);
    for (int t = 0; t < depth; t++) {
        my_ind[t] = {static_cast<SimDir>(rand()%4), static_cast<SimDir>(rand()%4)};
        opp_ind[t] = {static_cast<SimDir>(rand()%4), static_cast<SimDir>(rand()%4)};
    }
    std::vector<int> my_ids = {0, 1}, opp_ids = {2, 3};

    int EN = 10000;

    // Full eval with heuristics every step (cumulative)
    auto t_full = bench(EN, [&]{
        SimState sim = state;
        double score = 0.0;
        for (int t = 0; t < depth; t++) {
            if (sim.game_over) break;
            for (int s = 0; s < 2; s++) sim.set_dir(my_ids[s], my_ind[t][s]);
            for (int s = 0; s < 2; s++) sim.set_dir(opp_ids[s], opp_ind[t][s]);
            sim.step();
            double w = 1.0 / (1.0 + t);
            score += (sim.eval(0) + sim.energy_proximity(0) - sim.energy_proximity(1)
                     + sim.height_advantage(0) - sim.height_advantage(1)
                     + sim.territory(0)) * w;
        }
        (void)score;
    });
    fprintf(stderr, "  full evaluate      %6.1f us/call\n", (double)t_full / EN / 1000);

    // Just step x10 (no heuristics)
    auto t_step_only = bench(EN, [&]{
        SimState sim = state;
        for (int t = 0; t < depth; t++) {
            if (sim.game_over) break;
            for (int s = 0; s < 2; s++) sim.set_dir(my_ids[s], my_ind[t][s]);
            for (int s = 0; s < 2; s++) sim.set_dir(opp_ids[s], opp_ind[t][s]);
            sim.step();
        }
    });
    fprintf(stderr, "  steps-only x10     %6.1f us/call\n", (double)t_step_only / EN / 1000);
    fprintf(stderr, "  heuristics x10     %6.1f us/call (full - steps)\n",
            ((double)t_full - t_step_only) / EN / 1000);

    // --- Budget analysis ---
    fprintf(stderr, "\n--- Budget analysis (38ms turn) ---\n");
    double eval_us = (double)t_full / EN / 1000;
    double budget_us = 38000.0;
    double opp_budget = budget_us * 0.4;  // 40% for opp
    double my_budget = budget_us * 0.6;   // 60% for us
    int pop = 20;

    fprintf(stderr, "  eval cost:         %.1f us\n", eval_us);
    fprintf(stderr, "  total budget:      %.0f us\n", budget_us);
    fprintf(stderr, "  opp phase (40%%):   ~%.0f evals (%.0f us / %.1f us)\n", opp_budget / eval_us, opp_budget, eval_us);
    fprintf(stderr, "  my phase (60%%):    ~%.0f evals (%.0f us / %.1f us)\n", my_budget / eval_us, my_budget, eval_us);
    fprintf(stderr, "  init pop (%d):      %d evals\n", pop, pop);
    fprintf(stderr, "  GA generations:    ~%.0f (remaining after init)\n", (my_budget / eval_us - pop));

    // --- Breakdown pie chart ---
    fprintf(stderr, "\n--- Time breakdown per evaluate() call ---\n");
    double copy_ns = (double)t_copy / N;
    double steps_ns = (double)t_step_only / EN;
    double heur_ns = ((double)t_full - t_step_only) / EN;
    double total_ns = (double)t_full / EN;

    fprintf(stderr, "  SimState copy:     %5.1f%%  (%6.1f ns)\n", copy_ns / total_ns * 100, copy_ns);
    fprintf(stderr, "  10x step():        %5.1f%%  (%6.1f ns)\n", steps_ns / total_ns * 100, steps_ns);
    fprintf(stderr, "  10x heuristics:    %5.1f%%  (%6.1f ns)\n", heur_ns / total_ns * 100, heur_ns);
    fprintf(stderr, "  total:             100.0%%  (%6.1f ns = %.1f us)\n", total_ns, total_ns/1000);

    fprintf(stderr, "\nDone.\n");
    return 0;
}
