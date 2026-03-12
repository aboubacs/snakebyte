#pragma GCC optimize("-O3")
#pragma GCC optimize("inline")
#pragma GCC optimize("omit-frame-pointer")
#pragma GCC optimize("unroll-loops")

#include "bot.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>

int main() {
    srand(time(nullptr) ^ getpid());

    Bot bot;

#ifdef BOT_DEPTH
    bot.depth = BOT_DEPTH;
#endif
#ifdef BOT_POP
    bot.pop_size = BOT_POP;
#endif
#ifdef BOT_MUTRATE
    bot.mutation_rate = BOT_MUTRATE / 100.0;
#endif
#ifdef BOT_CUMEVAL
    bot.cumulative_eval = BOT_CUMEVAL;
#endif
#ifdef BOT_ELITE_K
    bot.elite_k = BOT_ELITE_K;
#endif
#ifdef BOT_ENERGY_K
    bot.energy_k = BOT_ENERGY_K;
#endif
#ifdef BOT_EVAL_DECAY
    bot.eval_decay = BOT_EVAL_DECAY;
#endif
#ifdef BOT_CHEAT_FACTOR
    bot.cheat_factor = BOT_CHEAT_FACTOR;
#endif
#ifdef BOT_CENTER_CONTROL_FACTOR
    bot.center_control_factor = BOT_CENTER_CONTROL_FACTOR / 100.0;
#endif

    bot.init();

    while (true) {
        bot.read_turn();
        bot.think();
    }

    return 0;
}
