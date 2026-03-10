#include "referee.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <chrono>

static const int FIRST_TURN_TIMEOUT_MS = 1000;
static const int TURN_TIMEOUT_MS = 50;

struct Player {
    pid_t pid = -1;
    int to_child_fd = -1;
    int from_child_fd = -1;
    FILE* to_child = nullptr;
    FILE* from_child = nullptr;
    std::string cmd;
};

bool launch_player(Player& p) {
    int pipe_in[2], pipe_out[2];
    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
        perror("pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    }

    if (pid == 0) {
        setsid();  // new process group so we can kill the whole tree
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        execl("/bin/sh", "sh", "-c", p.cmd.c_str(), nullptr);
        perror("execl");
        _exit(1);
    }

    close(pipe_in[0]);
    close(pipe_out[1]);
    p.pid = pid;
    p.to_child_fd = pipe_in[1];
    p.from_child_fd = pipe_out[0];
    p.to_child = fdopen(pipe_in[1], "w");
    p.from_child = fdopen(pipe_out[0], "r");
    if (!p.to_child || !p.from_child) {
        perror("fdopen");
        return false;
    }
    setbuf(p.to_child, nullptr);
    return true;
}

void send_to_player(Player& p, const std::string& msg) {
    fprintf(p.to_child, "%s", msg.c_str());
    fflush(p.to_child);
}

// Read a line from player with absolute deadline.
// Returns {line, true} on success, {"", false} on timeout/error.
std::pair<std::string, bool> read_from_player(Player& p,
        std::chrono::steady_clock::time_point deadline) {
    std::string line;
    struct pollfd pfd;
    pfd.fd = p.from_child_fd;
    pfd.events = POLLIN;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
        if (remaining <= 0) return {"", false};

        int ret = poll(&pfd, 1, remaining);
        if (ret <= 0) return {"", false};

        char c;
        ssize_t n = read(p.from_child_fd, &c, 1);
        if (n <= 0) return {"", false};
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return {line, true};
}

void cleanup_players(std::vector<Player>& players) {
    for (auto& p : players) {
        if (p.to_child) fclose(p.to_child);
        if (p.from_child) fclose(p.from_child);
        if (p.pid > 0) {
            // Kill entire process group (shell + bot child)
            kill(-p.pid, SIGTERM);
            // Brief wait, then force kill if still alive
            int status;
            pid_t ret = waitpid(p.pid, &status, WNOHANG);
            if (ret == 0) {
                usleep(50000);  // 50ms grace
                kill(-p.pid, SIGKILL);
                waitpid(p.pid, nullptr, 0);
            }
        }
    }
}

static std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <player1_cmd> <player2_cmd>" << std::endl;
        return 1;
    }

    int num_players = 2;
    std::vector<Player> players(num_players);
    for (int i = 0; i < num_players; i++) {
        players[i].cmd = argv[i + 1];
        if (!launch_player(players[i])) {
            std::cerr << "Failed to launch player " << i << std::endl;
            cleanup_players(players);
            return 1;
        }
    }

    // Initialize game
    GameState state;
    state.init_default_map();

    std::cerr << "[referee] === MATCH START ===" << std::endl;
    std::cerr << "[referee] Map " << state.width << "x" << state.height
              << ", " << state.snakes.size() << " snakes, "
              << state.energy.size() << " energy" << std::endl;
    for (int i = 0; i < num_players; i++) {
        std::cerr << "[referee] P" << i << ": " << players[i].cmd << std::endl;
    }

    // Send init input to each player
    for (int i = 0; i < num_players; i++) {
        std::string init = state.build_init_input(i);
        send_to_player(players[i], init);
    }

    // Collect frames
    std::vector<std::string> frames;
    std::vector<std::vector<std::string>> all_actions;

    // Initial frame (before any moves)
    frames.push_back(state.frame_json());
    all_actions.push_back({});

    // Game loop
    bool first_turn = true;
    std::string end_reason = "normal";
    while (!state.game_over) {
        int timeout = first_turn ? FIRST_TURN_TIMEOUT_MS : TURN_TIMEOUT_MS;

        // Send turn input to all players, then start the clock
        std::string turn_input = state.build_turn_input();
        for (int i = 0; i < num_players; i++) {
            send_to_player(players[i], turn_input);
        }
        auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout);

        // Read actions (all players share the same deadline)
        std::vector<std::string> turn_actions;
        for (int i = 0; i < num_players; i++) {
            auto [action, ok] = read_from_player(players[i], deadline);
            turn_actions.push_back(action);

            if (!ok) {
                std::cerr << "Player " << i << " timed out (" << timeout << "ms)" << std::endl;
                end_reason = "timeout_p" + std::to_string(i);
                state.game_over = true;
                state.winner = 1 - i;
                break;
            }

            if (!state.parse_actions(i, action)) {
                std::cerr << "Invalid action from player " << i << ": " << action << std::endl;
                end_reason = "invalid_action_p" + std::to_string(i);
                state.game_over = true;
                state.winner = 1 - i;
                break;
            }
        }

        first_turn = false;

        if (state.game_over) {
            all_actions.push_back(turn_actions);
            frames.push_back(state.frame_json());
            break;
        }

        // Execute turn
        state.step();

        if (state.turn % 50 == 0) {
            int s0 = state.count_body_parts(0);
            int s1 = state.count_body_parts(1);
            std::cerr << "[referee] Turn " << state.turn
                      << ": " << s0 << "-" << s1 << std::endl;
        }

        all_actions.push_back(turn_actions);
        frames.push_back(state.frame_json());
    }

    cleanup_players(players);

    // Output JSON game log
    int score0 = state.count_body_parts(0);
    int score1 = state.count_body_parts(1);

    std::string winner_str = state.winner >= 0
        ? "P" + std::to_string(state.winner)
        : "draw";
    std::cerr << "[referee] End turn " << state.turn
              << ": " << score0 << "-" << score1
              << " (" << winner_str << ")" << std::endl;

    std::cout << "{\"num_players\":" << num_players
              << ",\"num_turns\":" << state.turn
              << ",\"winner\":" << state.winner
              << ",\"end_reason\":\"" << end_reason << "\""
              << ",\"scores\":[" << score0 << "," << score1 << "]"
              << ",\"players\":[\"" << escape_json(players[0].cmd) << "\",\""
              << escape_json(players[1].cmd) << "\"]"
              << "," << state.game_json_header()
              << ",\"frames\":[";

    for (size_t t = 0; t < frames.size(); t++) {
        if (t > 0) std::cout << ",";
        std::cout << "{\"state\":" << frames[t] << ",\"actions\":[";
        for (size_t i = 0; i < all_actions[t].size(); i++) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << escape_json(all_actions[t][i]) << "\"";
        }
        std::cout << "]}";
    }

    std::cout << "]}" << std::endl;

    return 0;
}
