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

    bot.init();

    while (true) {
        bot.read_turn();
        bot.think();
    }

    return 0;
}
