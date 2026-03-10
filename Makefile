CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
PYTHON := python3

.PHONY: bot-mc bot-ga bot-gao bot-dga bot-cga bot-sga referee merge version run gui league clean

bot-mc:
	$(CXX) $(CXXFLAGS) -o bot.out src/mc/main.cpp src/mc/bot.cpp src/sim.cpp

bot-ga:
	$(CXX) $(CXXFLAGS) -o bot.out src/ga/main.cpp src/ga/bot.cpp src/sim.cpp

bot-gao:
	$(CXX) $(CXXFLAGS) -o bot.out src/ga_opp/main.cpp src/ga_opp/bot.cpp src/sim.cpp

bot-dga:
	$(CXX) $(CXXFLAGS) -o bot.out src/dga/main.cpp src/dga/bot.cpp src/sim.cpp

bot-cga:
	$(CXX) $(CXXFLAGS) -o bot.out src/cga/main.cpp src/cga/bot.cpp src/sim.cpp

bot-sga:
	$(CXX) $(CXXFLAGS) -o bot.out src/sga/main.cpp src/sga/bot.cpp src/sim.cpp

referee:
	$(CXX) $(CXXFLAGS) -o referee.out referee/main.cpp referee/referee.cpp

merge:
ifndef BOT
	$(error BOT is required: make merge BOT=mc)
endif
	$(PYTHON) tools/merge.py $(BOT)

version:
ifndef BOT
	$(error BOT is required: make version BOT=mc V=mc_d5)
endif
	$(PYTHON) tools/merge.py $(BOT)
ifdef V
	$(CXX) $(CXXFLAGS) -o builds/$(V).out merged/merged.cpp
	@echo "Built builds/$(V).out"
else
	@V=$$(printf "v%03d" $$(( $$(ls builds/*.out 2>/dev/null | sed 's/.*v\([0-9]*\)\.out/\1/' | sort -n | tail -1) + 1 ))); \
	$(CXX) $(CXXFLAGS) -o builds/$$V.out merged/merged.cpp; \
	echo "Built builds/$$V.out"
endif

run:
ifndef P
	$(error P is required: make run P="./builds/v001.out ./builds/v002.out")
endif
	$(PYTHON) tools/runner.py $(P)

gui:
	$(PYTHON) web/server.py

league:
ifndef POOL
	$(error POOL is required: make league POOL=pool1)
endif
	$(PYTHON) tools/league.py $(POOL)

clean:
	rm -f bot.out referee.out
	rm -f merged/merged.cpp
