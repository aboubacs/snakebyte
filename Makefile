CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
PYTHON := python3

.PHONY: bot referee merge version run gui league clean

bot:
	$(CXX) $(CXXFLAGS) -o bot.out src/main.cpp src/bot.cpp src/sim.cpp

referee:
	$(CXX) $(CXXFLAGS) -o referee.out referee/main.cpp referee/referee.cpp

merge:
	$(PYTHON) tools/merge.py

version:
	$(PYTHON) tools/merge.py
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
