# SaklıBahçe — build (macOS: Apple clang + Homebrew raylib 6.0; Linux: clang/gcc + raylib 6.0 from source)
#   make            -> ./saklibahce (the game)
#   make run        -> build and start the game
#   make test       -> engine + AI tests and short headless bot-vs-bot simulations
#   make asan       -> AddressSanitizer + UBSan build in build/asan/ (game + tests), then runs the tests
#   make clean
# Only src/ is compiled into the game; tools/ (developer snapshot harnesses) is not.
UNAME_S  := $(shell uname -s)
ifeq ($(origin CXX),default)
CXX      := clang++
endif
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS  ?=
ifeq ($(UNAME_S),Darwin)
# raylib's prefix: Homebrew on Apple Silicon (/opt/homebrew) or Intel (/usr/local); override with RAYLIB=...
RAYLIB   ?= $(or $(patsubst %/lib/libraylib.a,%,$(firstword $(wildcard /opt/homebrew/lib/libraylib.a \
                                                                  /usr/local/lib/libraylib.a))),/opt/homebrew)
SYS_LIBS := -framework Cocoa -framework IOKit -framework OpenGL \
            -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
else
# Linux: raylib 6 built from source (make install puts it in /usr/local) or a distro package; X11 + OpenGL
RAYLIB   ?= $(or $(patsubst %/lib/libraylib.a,%,$(firstword $(wildcard /usr/local/lib/libraylib.a \
                                                                  /usr/lib/libraylib.a))),/usr/local)
SYS_LIBS := -lGL -lm -lpthread -ldl -lrt -lX11
endif
CPPFLAGS := -Isrc -isystem $(RAYLIB)/include -MMD -MP
LDLIBS   := $(RAYLIB)/lib/libraylib.a $(SYS_LIBS)

BUILD    ?= build/make
GAME     ?= saklibahce
CORE_SRC := $(wildcard src/core/*.cpp)
UI_SRC   := $(wildcard src/ui/*.cpp) $(wildcard src/r3d/*.cpp)
APP_SRC  := $(wildcard src/app/*.cpp)
TEST_NAMES := test_engine test_ai sim test_tavla tavla_sim test_batak batak_sim test_king king_sim test_pisti pisti_sim test_banter test_analysis test_achievements
TEST_NAMES += test_altmisalti altmisalti_sim
TEST_NAMES += test_bezik bezik_sim # Bezik
TEST_NAMES += test_dama dama_sim # Dama
TEST_NAMES += test_konken konken_sim # Konken
TEST_NAMES += test_ozelgun # Özel günler (ozelgun)
TEST_SRC := $(TEST_NAMES:%=tests/%.cpp)
CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
UI_OBJ   := $(UI_SRC:%.cpp=$(BUILD)/%.o)
APP_OBJ  := $(APP_SRC:%.cpp=$(BUILD)/%.o)
TEST_OBJ := $(TEST_SRC:%.cpp=$(BUILD)/%.o)
TESTS    := $(TEST_NAMES:%=$(BUILD)/%)

ASAN_FLAGS := -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra

.PHONY: all test tests run clean asan tablescheck cardsnapshot basarimcheck catwalk
all: $(GAME)

$(GAME): $(CORE_OBJ) $(UI_OBJ) $(APP_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(TESTS): $(BUILD)/%: $(BUILD)/tests/%.o $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@
$(BUILD)/test_analysis: $(BUILD)/src/app/Analysis.o
$(BUILD)/test_analysis: $(BUILD)/src/app/AnalysisDama.o # Dama
$(BUILD)/test_analysis: $(BUILD)/src/app/AnalysisKonken.o # Konken

tests: $(TESTS)

test: $(TESTS)
	$(BUILD)/test_engine
	$(BUILD)/test_ai
	$(BUILD)/sim --hands 400 --seed 7
	$(BUILD)/sim --hands 400 --seed 11 --levels 2,0,1,2 --rotate
	$(BUILD)/sim --hands 200 --seed 13 --levels 2,1,2,1 --rotate --katlamali
	$(BUILD)/sim --hands 200 --seed 19 --levels 2,1,2,1 --rotate --esli
	$(BUILD)/sim --hands 100 --seed 17 --levels 2,1,0,2 --rotate --okey
	$(BUILD)/sim --hands 100 --seed 23 --levels 2,1,2,1 --rotate --tek-kat --acma 81 --acmayan 404 # 101 kuralları
	$(BUILD)/sim --hands 100 --seed 29 --levels 2,1,2,1 --rotate --katsiz --katlamali --esli # 101 kuralları
	$(BUILD)/test_tavla
	$(BUILD)/tavla_sim --games 200 --seed 3 --levels 2,1
	$(BUILD)/tavla_sim --games 100 --seed 3 --levels 2,1 --variant gulbahar
	$(BUILD)/tavla_sim --games 100 --seed 3 --levels 2,1 --variant fevga --doubling
	$(BUILD)/test_batak
	$(BUILD)/batak_sim --hands 100 --seed 3 --levels 2,1,0,1
	$(BUILD)/batak_sim --hands 60 --seed 4 --levels 2,1,2,1 --esli
	$(BUILD)/test_king
	$(BUILD)/king_sim --games 4 --seed 3 --levels 2,1,0,1
	$(BUILD)/test_pisti
	$(BUILD)/pisti_sim --hands 200 --seed 3 --levels 2,1,0,1
	$(BUILD)/pisti_sim --hands 100 --seed 4 --levels 2,1,2,1 --mode esli
	$(BUILD)/test_altmisalti
	$(BUILD)/altmisalti_sim --matches 12 --seed 3 --levels 2,1
	$(BUILD)/altmisalti_sim --matches 30 --seed 4 --levels 1,0
	$(BUILD)/test_bezik
	$(BUILD)/bezik_sim --hands 24 --seed 3 --levels 2,1
	$(BUILD)/bezik_sim --hands 200 --seed 4 --levels 1,0
	$(BUILD)/test_dama
	$(BUILD)/dama_sim --games 8 --seed 3 --levels 2,1
	$(BUILD)/dama_sim --games 20 --seed 4 --levels 1,0
	$(BUILD)/test_konken
	$(BUILD)/konken_sim --hands 60 --seed 3 --levels 2,1,0,1 --duplicate
	$(BUILD)/test_banter
	$(BUILD)/test_analysis
	$(BUILD)/test_achievements
	$(BUILD)/test_ozelgun # Özel günler

# The other table games played by the mouse in a hidden window (needs a display: run it awake, not over ssh)
TABLES_CHECK_OBJ := $(CORE_OBJ) $(UI_OBJ) $(filter-out $(BUILD)/src/app/App%.o $(BUILD)/src/app/main.o,$(APP_OBJ))
$(BUILD)/tables_check: $(BUILD)/tools/tables_check.o $(TABLES_CHECK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
# Linux without a display (the cloud, CI): a virtual one through xvfb-run. TABLESCHECK_ARGS=--no-3d skips the 3D
# pass (a software GL draws a frame in ~0.1 s: the full run takes ~45 min, without 3D a few minutes).
TABLESCHECK_ARGS ?=
ifneq ($(UNAME_S),Darwin)
ifeq ($(DISPLAY),)
HEADLESS := $(if $(shell command -v xvfb-run),xvfb-run -a -s "-screen 0 1920x1080x24")
endif
endif
# The regulars' card-game hands close up (tools/cards_snapshot.cpp): pictures into build/cards/
$(BUILD)/cards_snapshot: $(BUILD)/tools/cards_snapshot.o $(TABLES_CHECK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
cardsnapshot: $(BUILD)/cards_snapshot
	@mkdir -p build/cards
	$(HEADLESS) $(BUILD)/cards_snapshot build/cards

# (duzelt, round 5) Başarımlar end to end: a real match (Pişti, then a one-point tavla) through the attended path,
# the book in a throwaway HOME; passes when a badge opens and its banner, a regular's congratulation and the room's
# applause are all seen (src/app/AppAchievements.cpp, --achievement-test). The cat's floor plan:
# room_snapshot's "catwalk" (half an hour of the room inside and in the garden, with and without people standing).
basarimcheck: $(GAME)
	@d=$$(mktemp -d); for g in "pisti" "tavla --set tavla=1"; do \
	  HOME=$$d $(HEADLESS) ./$(GAME) --achievement-test --seed 3 --game $$g --speed 4 --no-audio \
	    --snapshot $$d/basarim.png --state game --frames 400000 > $$d/out.txt; r=$$?; \
	  grep basarim-test $$d/out.txt; [ $$r -eq 0 ] || exit 1; done; rm -rf $$d
$(BUILD)/room_snapshot: $(BUILD)/tools/room_snapshot.o $(TABLES_CHECK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
catwalk: $(BUILD)/room_snapshot
	$(HEADLESS) $(BUILD)/room_snapshot build/room 1800 catwalk

# Rakip (round 5): the opponent's hands at the tavla table close up (tools/board_snapshot.cpp): pictures into build/board/
$(BUILD)/board_snapshot: $(BUILD)/tools/board_snapshot.o $(TABLES_CHECK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
boardsnapshot: $(BUILD)/board_snapshot
	@mkdir -p build/board
	$(HEADLESS) $(BUILD)/board_snapshot build/board

tablescheck: $(BUILD)/tables_check
	$(HEADLESS) $(BUILD)/tables_check --hands 2 $(TABLESCHECK_ARGS)

run: $(GAME)
	./$(GAME)

# Sanitized build: build/asan/saklibahce (try: build/asan/saklibahce --ai --speed 8 --no-audio)
asan:
	$(MAKE) BUILD=build/asan GAME=build/asan/saklibahce CXXFLAGS="$(ASAN_FLAGS)" LDFLAGS="-fsanitize=address,undefined" \
	    build/asan/saklibahce tests
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_engine
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_ai
	ASAN_OPTIONS=detect_leaks=0 build/asan/sim --hands 200 --seed 5 --levels 2,1,0,2
	ASAN_OPTIONS=detect_leaks=0 build/asan/sim --hands 40 --seed 6 --levels 2,1,0,2 --okey
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_tavla
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_batak
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_king
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_pisti
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_altmisalti
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_bezik
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_dama
	ASAN_OPTIONS=detect_leaks=0 build/asan/test_konken

# Optional static checks (fixes, 2026-10): clang-tidy over src/ with a small check set; skipped (not a failure) when
# clang-tidy is not installed (macOS: brew install llvm, it is then in $(brew --prefix llvm)/bin). Narrow it with
# TIDY_SRC=src/core/Game.cpp, change the set with TIDY_CHECKS=... (findings are reported, they do not fail the build).
CLANG_TIDY  ?= $(or $(shell command -v clang-tidy 2>/dev/null),$(wildcard /opt/homebrew/opt/llvm/bin/clang-tidy),$(wildcard /usr/local/opt/llvm/bin/clang-tidy))
TIDY_CHECKS ?= -*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-bugprone-implicit-widening-of-multiplication-result,performance-*,-performance-enum-size,misc-redundant-expression,readability-misleading-indentation
TIDY_SRC    ?= $(CORE_SRC) $(UI_SRC) $(APP_SRC)
.PHONY: tidy
tidy:
	@if [ -z "$(CLANG_TIDY)" ]; then echo "make tidy: clang-tidy not found (brew install llvm), skipped"; else \
	    $(CLANG_TIDY) --quiet -checks='$(TIDY_CHECKS)' $(TIDY_SRC) -- -std=c++17 -Isrc -isystem $(RAYLIB)/include || true; fi

clean:
	rm -rf build/make build/asan $(GAME)

-include $(CORE_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
