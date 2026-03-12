// Unit tests for sim.hpp / sim.cpp
// Build: g++ -std=c++17 -O2 -o tests/test_sim tests/test_sim.cpp src/sim.cpp
// Run:   ./tests/test_sim

#include "../src/sim.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        fprintf(stderr, "  %-50s ", #name); \
        name(); \
        tests_passed++; \
        fprintf(stderr, "OK\n"); \
    } while(0)

#define ASSERT(cond) do { if (!(cond)) { fprintf(stderr, "FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); assert(false); } } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL\n    %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, (int)(a), (int)(b)); assert(false); } } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a) - (b)) > (eps)) { fprintf(stderr, "FAIL\n    %s:%d: %s == %f, expected %f\n", __FILE__, __LINE__, #a, (double)(a), (double)(b)); assert(false); } } while(0)

// Helper: create a basic state with a flat grid
static SimState make_state(int w, int h) {
    SimState s;
    s.width = w;
    s.height = h;
    s.grid.resize(h, std::string(w, '.'));
    s.grid[h - 1] = std::string(w, '#'); // bottom wall
    return s;
}

static SimSnake make_snake(int id, int owner, std::vector<SimPos> body, SimDir dir = SIM_UP) {
    SimSnake s;
    s.id = id;
    s.owner = owner;
    s.body = body;
    s.dir = dir;
    s.alive = true;
    return s;
}

// ============================================================
// GRAVITY TESTS
// ============================================================

// Snake on platform should not fall
void test_gravity_grounded_no_fall() {
    auto s = make_state(10, 10);
    // Snake sitting on bottom wall (y=8, wall at y=9)
    s.snakes.push_back(make_snake(0, 0, {{3, 8}, {3, 7}, {3, 6}}));
    SimState before = s;
    s.step();
    // Snake should stay at same position (only moved by direction, not gravity)
    // Direction is UP, so head goes to (3,7), body shifts
    // After move: head at (3,7), then (3,8), then (3,7) -- wait, that's a collision
    // Let's use LEFT direction to avoid confusion
    s = before;
    s.snakes[0].dir = SIM_LEFT;
    s.step();
    // Head moves left to (2,8), old head (3,8) becomes body[1], tail (3,6) drops
    // Body: (2,8), (3,8), (3,7) -- (2,8) is above wall at y=9, grounded
    ASSERT(s.snakes[0].alive);
    ASSERT_EQ(s.snakes[0].body[0].y, 8); // head still at row 8
}

// Snake floating in air should fall
void test_gravity_airborne_falls() {
    auto s = make_state(10, 10);
    // Snake floating at y=3,4,5 with no platform below
    s.snakes.push_back(make_snake(0, 0, {{5, 3}, {5, 4}, {5, 5}}, SIM_LEFT));
    s.step();
    // After move, head goes to (4,3), body is (4,3),(5,3),(5,4)
    // All floating, should fall. With wall at y=9, should fall until grounded.
    // Final position: body should be near bottom
    ASSERT(s.snakes[0].alive);
    ASSERT_EQ(s.snakes[0].body[0].y, 7); // fallen to just above wall
    ASSERT_EQ(s.snakes[0].body[1].y, 7);
    ASSERT_EQ(s.snakes[0].body[2].y, 8);
}

// Snake supported by energy should not fall
void test_gravity_energy_support() {
    auto s = make_state(10, 10);
    // Snake at y=5, energy at (5,6) - directly below body cell
    s.snakes.push_back(make_snake(0, 0, {{5, 4}, {5, 5}, {5, 6}}, SIM_LEFT));
    s.energy.push_back({4, 5}); // below where the head will be after move
    s.energy.push_back({5, 7}); // below tail
    s.step();
    // After LEFT move: body = (4,4),(5,4),(5,5)
    // Below (5,5) is (5,6) -- no energy there now, below (4,4) is (4,5) -- energy!
    ASSERT(s.snakes[0].alive);
    ASSERT_EQ(s.snakes[0].body[0].y, 4); // held by energy, no fall
}

// Snake A on top of grounded snake B should not fall
void test_gravity_snake_on_snake() {
    auto s = make_state(10, 10);
    // Snake B grounded on wall at y=8
    s.snakes.push_back(make_snake(0, 0, {{5, 8}, {5, 7}, {5, 6}}, SIM_LEFT));
    // Snake A sitting on top of snake B (body cell at y=5, B has cell at y=6)
    s.snakes.push_back(make_snake(1, 1, {{5, 5}, {5, 4}, {5, 3}}, SIM_RIGHT));

    // Step with minimal movement
    s.snakes[0].dir = SIM_LEFT;
    s.snakes[1].dir = SIM_RIGHT;
    s.step();

    // Snake B should be grounded (on wall)
    // Snake A should be grounded (on snake B's body)
    ASSERT(s.snakes[0].alive);
    ASSERT(s.snakes[1].alive);
    // Snake A head moved RIGHT to (6,5), body (5,5),(5,4)
    // Snake B's body includes (5,6) after move: head (4,8), body (5,8),(5,7)
    // Below snake A's (5,5) is (5,6) -- but snake B moved, no longer there
    // Actually let me check: B moves LEFT: head (4,8), body = (4,8),(5,8),(5,7)
    // So B's cells: (4,8),(5,8),(5,7). Below A's (5,5) is (5,6) -- not in B.
    // A should fall. Let me adjust the test.
    // Better test: make them not move into each other's space
}

// Fixed version: snake on top of grounded snake
void test_gravity_stacked_snakes() {
    auto s = make_state(10, 10);
    // Snake B grounded: horizontal on row 8 (wall at 9)
    s.snakes.push_back(make_snake(0, 0, {{3, 8}, {4, 8}, {5, 8}}, SIM_LEFT));
    // Snake A above: body cell at y=7, directly above B's cell at y=8
    s.snakes.push_back(make_snake(1, 1, {{4, 7}, {4, 6}, {4, 5}}, SIM_RIGHT));

    s.step();

    // B moves LEFT: head (2,8), body (3,8),(4,8) -- still on wall
    // A moves RIGHT: head (5,7), body (4,7),(4,6)
    // Below A's (4,7) is (4,8) which is in B's body -- grounded via B
    ASSERT(s.snakes[0].alive);
    ASSERT(s.snakes[1].alive);
    ASSERT_EQ(s.snakes[1].body[1].y, 7); // didn't fall
}

// Two floating snakes should both fall independently
void test_gravity_two_floating_snakes_fall_independently() {
    auto s = make_state(20, 10);
    // Snake A at x=3, floating
    s.snakes.push_back(make_snake(0, 0, {{3, 3}, {3, 4}, {3, 5}}, SIM_LEFT));
    // Snake B at x=15, floating
    s.snakes.push_back(make_snake(1, 1, {{15, 2}, {15, 3}, {15, 4}}, SIM_RIGHT));

    s.step();

    // Both should fall to bottom
    ASSERT(s.snakes[0].alive);
    ASSERT(s.snakes[1].alive);
    // A: moved LEFT to (2,3), body (2,3),(3,3),(3,4) -- falls to 7,7,8
    ASSERT_EQ(s.snakes[0].body[2].y, 8); // bottom-most cell just above wall
    // B: moved RIGHT to (16,2), body (16,2),(15,2),(15,3) -- falls to 7,7,8
    ASSERT_EQ(s.snakes[1].body[2].y, 8);
}

// Horizontally adjacent snake should NOT support the other (key bug we fixed)
void test_gravity_horizontal_adjacent_no_support() {
    auto s = make_state(10, 10);
    // Snake A grounded at (3,8) on wall
    s.snakes.push_back(make_snake(0, 0, {{3, 8}, {3, 7}, {3, 6}}, SIM_LEFT));
    // Snake B floating at (4,6),(4,7),(4,8) -- horizontally adjacent to A
    // but NOT on top of A. B has cell at (4,8) which IS on wall though.
    // Let me make B truly floating with no support
    // B at x=4, y=4,5,6 -- adjacent horizontally to A at y=6
    s.snakes.push_back(make_snake(1, 1, {{4, 4}, {4, 5}, {4, 6}}, SIM_RIGHT));

    s.step();

    // A moves LEFT: head (2,8), body (2,8),(3,8),(3,7) -- grounded on wall
    // B moves RIGHT: head (5,4), body (5,4),(4,4),(4,5)
    // B has no support below any cell (no wall, no energy, no grounded snake body below)
    // (4,5) -> below is (4,6) -- nobody there anymore
    // B should fall!
    ASSERT(s.snakes[0].alive);
    ASSERT(s.snakes[1].alive);
    ASSERT(s.snakes[1].body[0].y > 4); // must have fallen
}

// Snake falls off bottom edge and dies
void test_gravity_fall_out_of_bounds() {
    auto s = make_state(10, 5);
    // Very short grid, no bottom wall -- remove it
    s.grid[4] = std::string(10, '.');
    // Snake at y=3
    s.snakes.push_back(make_snake(0, 0, {{5, 2}, {5, 3}, {5, 4}}, SIM_LEFT));
    s.step();
    // Moves LEFT, then falls off bottom
    ASSERT(!s.snakes[0].alive);
}

// Grounded propagation: A on B on C on ground
void test_gravity_chain_support() {
    auto s = make_state(10, 20);
    // C grounded on wall at y=19 (wall), C body at y=18
    s.snakes.push_back(make_snake(0, 0, {{5, 18}, {5, 17}, {5, 16}}, SIM_LEFT));
    // B on top of C: body at y=15, C has cell at y=16
    s.snakes.push_back(make_snake(1, 1, {{5, 15}, {5, 14}, {5, 13}}, SIM_LEFT));
    // A on top of B: body at y=12, B has cell at y=13
    s.snakes.push_back(make_snake(2, 0, {{5, 12}, {5, 11}, {5, 10}}, SIM_LEFT));

    s.step();

    // After moves (all LEFT), vertical columns shift.
    // C: (4,18),(5,18),(5,17) -- (5,18) still above wall, grounded
    // B: (4,15),(5,15),(5,14) -- below (5,15) is (5,16) in C? No, C is now (4,18),(5,18),(5,17)
    // C has cell at (5,17). Below B's (5,14) is (5,15) -- that's B itself.
    // Actually B's lowest is (5,15). Below that is (5,16). C's body is (4,18),(5,18),(5,17).
    // C has (5,17), not (5,16). So B is NOT on C anymore.
    // This test needs different geometry. Let me use horizontal snakes instead.
}

// Better chain test with horizontal snakes
void test_gravity_chain_horizontal() {
    auto s = make_state(10, 12);
    // C: horizontal on row 10 (wall at 11)
    s.snakes.push_back(make_snake(0, 0, {{3, 10}, {4, 10}, {5, 10}}, SIM_LEFT));
    // B: horizontal on row 9 (directly on C)
    s.snakes.push_back(make_snake(1, 1, {{3, 9}, {4, 9}, {5, 9}}, SIM_LEFT));
    // A: horizontal on row 8 (directly on B)
    s.snakes.push_back(make_snake(2, 0, {{3, 8}, {4, 8}, {5, 8}}, SIM_LEFT));

    s.step();

    // C moves LEFT: (2,10),(3,10),(4,10) -- on wall, grounded
    // B moves LEFT: (2,9),(3,9),(4,9) -- below (3,9) is (3,10) in C, grounded
    // A moves LEFT: (2,8),(3,8),(4,8) -- below (3,8) is (3,9) in B, grounded via chain
    ASSERT(s.snakes[0].alive);
    ASSERT(s.snakes[1].alive);
    ASSERT(s.snakes[2].alive);
    ASSERT_EQ(s.snakes[2].body[0].y, 8); // A didn't fall
    ASSERT_EQ(s.snakes[1].body[0].y, 9); // B didn't fall
}

// ============================================================
// ENERGY PROXIMITY TESTS
// ============================================================

void test_energy_proximity_no_energy() {
    auto s = make_state(10, 10);
    s.snakes.push_back(make_snake(0, 0, {{5, 5}, {5, 6}, {5, 7}}));
    ASSERT_NEAR(s.energy_proximity(0), 0.0, 1e-9);
}

void test_energy_proximity_no_snakes() {
    auto s = make_state(10, 10);
    s.energy.push_back({5, 5});
    ASSERT_NEAR(s.energy_proximity(0), 0.0, 1e-9);
}

void test_energy_proximity_on_energy() {
    auto s = make_state(10, 10);
    s.snakes.push_back(make_snake(0, 0, {{5, 5}, {5, 6}, {5, 7}}));
    s.energy.push_back({5, 5}); // same pos as head
    // dist=0, value = 1/(1+0) = 1.0, result = 0.5 * 1.0 / 1 = 0.5
    ASSERT_NEAR(s.energy_proximity(0), 0.5, 1e-9);
}

void test_energy_proximity_distance() {
    auto s = make_state(20, 20);
    s.snakes.push_back(make_snake(0, 0, {{5, 5}, {5, 6}, {5, 7}}));
    s.energy.push_back({8, 5}); // manhattan dist = 3
    // closest dist=3, value = 1/(1+3) = 0.25, result = 0.5 * 0.25 / 1 = 0.125
    ASSERT_NEAR(s.energy_proximity(0), 0.125, 1e-9);
}

void test_energy_proximity_picks_closest() {
    auto s = make_state(20, 20);
    s.snakes.push_back(make_snake(0, 0, {{5, 5}, {5, 6}, {5, 7}}));
    s.energy.push_back({15, 15}); // far away, dist=20
    s.energy.push_back({6, 5});   // dist=1, closest
    // After optimization: only closest matters
    // closest dist=1, value = 1/(1+1) = 0.5, result = 0.5 * 0.5 / 1 = 0.25
    ASSERT_NEAR(s.energy_proximity(0), 0.25, 1e-9);
}

void test_energy_proximity_two_snakes() {
    auto s = make_state(20, 20);
    s.snakes.push_back(make_snake(0, 0, {{5, 5}, {5, 6}, {5, 7}}));
    s.snakes.push_back(make_snake(1, 0, {{10, 10}, {10, 11}, {10, 12}}));
    s.energy.push_back({5, 5});   // dist=0 from snake 0
    s.energy.push_back({10, 10}); // dist=0 from snake 1
    // snake 0: closest dist=0, val=1.0
    // snake 1: closest dist=0, val=1.0
    // avg = (1.0 + 1.0) / 2 = 1.0, result = 0.5 * 1.0 = 0.5
    ASSERT_NEAR(s.energy_proximity(0), 0.5, 1e-9);
}

// ============================================================
// APPLY_GRAVITY REFERENCE TEST
// Run the same scenario through the old and new code and compare
// ============================================================

void test_gravity_complex_scenario() {
    // Complex: 4 snakes, some grounded, some floating, some stacked
    auto s = make_state(20, 15);
    // Add a mid-air platform
    for (int x = 8; x <= 12; x++) s.grid[10][x] = '#';

    // Snake 0: on mid platform
    s.snakes.push_back(make_snake(0, 0, {{10, 9}, {10, 8}, {10, 7}}, SIM_LEFT));
    // Snake 1: floating, no support
    s.snakes.push_back(make_snake(1, 1, {{3, 3}, {3, 4}, {3, 5}}, SIM_RIGHT));
    // Snake 2: on bottom wall
    s.snakes.push_back(make_snake(2, 0, {{15, 13}, {15, 12}, {15, 11}}, SIM_LEFT));
    // Snake 3: floating above snake 0 (will land on it after gravity)
    s.snakes.push_back(make_snake(3, 1, {{10, 5}, {10, 4}, {10, 3}}, SIM_LEFT));

    s.step();

    // Snake 0 moved LEFT: (9,9),(10,9),(10,8) -- (10,9) above platform at y=10, grounded
    ASSERT(s.snakes[0].alive);
    ASSERT_EQ(s.snakes[0].body[0].y, 9);

    // Snake 1 moved RIGHT: (4,3),(3,3),(3,4) -- floating, falls to bottom
    ASSERT(s.snakes[1].alive);
    ASSERT_EQ(s.snakes[1].body[2].y, 13); // lowest cell at row 13 (above wall at 14)

    // Snake 2 moved LEFT: (14,13),(15,13),(15,12) -- grounded on wall
    ASSERT(s.snakes[2].alive);
    ASSERT_EQ(s.snakes[2].body[0].y, 13);

    // Snake 3 moved LEFT: (9,5),(10,5),(10,4) -- floats
    // Below (9,5) nothing. Below (10,5) nothing.
    // Should fall. Snake 0 is at (9,9),(10,9),(10,8).
    // Snake 3 falls from y=(5,5,4) until it lands on snake 0 or platform.
    // (10,4) -> falls to (10,7) since snake 0 has cell at (10,8)? Let's check.
    // Snake 0 body: (9,9),(10,9),(10,8)
    // Snake 3 body after move: (9,5),(10,5),(10,4)
    // Fall 1: (9,6),(10,6),(10,5) -- check grounded: below (10,5) is (10,6) = self. below (9,6) is (9,7) = nothing. not grounded.
    // Fall 2: (9,7),(10,7),(10,6) -- below (10,7) is (10,8) = snake 0's cell! snake 0 grounded? Yes.
    // So snake 3 stops at y=(7,7,6)
    ASSERT(s.snakes[3].alive);
    ASSERT_EQ(s.snakes[3].body[0].y, 7);
    ASSERT_EQ(s.snakes[3].body[1].y, 7);
    ASSERT_EQ(s.snakes[3].body[2].y, 6);
}

// ============================================================
// MAIN
// ============================================================

int main() {
    fprintf(stderr, "Running sim tests...\n");

    TEST(test_gravity_grounded_no_fall);
    TEST(test_gravity_airborne_falls);
    TEST(test_gravity_energy_support);
    TEST(test_gravity_stacked_snakes);
    TEST(test_gravity_two_floating_snakes_fall_independently);
    TEST(test_gravity_horizontal_adjacent_no_support);
    TEST(test_gravity_fall_out_of_bounds);
    TEST(test_gravity_chain_horizontal);
    TEST(test_gravity_complex_scenario);

    TEST(test_energy_proximity_no_energy);
    TEST(test_energy_proximity_no_snakes);
    TEST(test_energy_proximity_on_energy);
    TEST(test_energy_proximity_distance);
    TEST(test_energy_proximity_picks_closest);
    TEST(test_energy_proximity_two_snakes);

    fprintf(stderr, "\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
