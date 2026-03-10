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
#ifdef BOT_ENERGY_K
    bot.energy_k = BOT_ENERGY_K;
#endif
#ifdef BOT_OPP_DEPTH
    bot.opp_depth = BOT_OPP_DEPTH;
#endif
#ifdef BOT_OPP_TIME_PCT
    bot.opp_time_pct = BOT_OPP_TIME_PCT / 100.0;
#endif

    bot.init();

    while (true) {
        bot.read_turn();
        bot.think();
    }

    return 0;
}
