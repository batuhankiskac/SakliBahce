# SaklıBahçe — build (macOS, Apple clang, raylib 6.0 from Homebrew)
#   make            -> ./saklibahce (the game)
#   make run        -> build and start the game
#   make test       -> engine + AI tests and short headless bot-vs-bot simulations
#   make asan       -> AddressSanitizer + UBSan build in build/asan/ (game + tests), then runs the tests
#   make clean
# Only src/ is compiled into the game; tools/ (developer snapshot harnesses) is not.
ifeq ($(origin CXX),default)
CXX      := clang++
endif
# raylib's prefix: Homebrew on Apple Silicon (/opt/homebrew) or Intel (/usr/local); override with RAYLIB=...
RAYLIB   ?= $(or $(patsubst %/lib/libraylib.a,%,$(firstword $(wildcard /opt/homebrew/lib/libraylib.a \
                                                                  /usr/local/lib/libraylib.a))),/opt/homebrew)
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDFLAGS  ?=
CPPFLAGS := -Isrc -isystem $(RAYLIB)/include -MMD -MP
LDLIBS   := $(RAYLIB)/lib/libraylib.a -framework Cocoa -framework IOKit -framework OpenGL \
            -framework CoreVideo -framework CoreAudio -framework AudioToolbox -framework CoreFoundation

BUILD    ?= build/make
GAME     ?= saklibahce
CORE_SRC := $(wildcard src/core/*.cpp)
UI_SRC   := $(wildcard src/ui/*.cpp) $(wildcard src/r3d/*.cpp)
APP_SRC  := $(wildcard src/app/*.cpp)
TEST_NAMES := test_engine test_ai sim test_tavla tavla_sim test_batak batak_sim test_king king_sim test_pisti pisti_sim
TEST_SRC := $(TEST_NAMES:%=tests/%.cpp)
CORE_OBJ := $(CORE_SRC:%.cpp=$(BUILD)/%.o)
UI_OBJ   := $(UI_SRC:%.cpp=$(BUILD)/%.o)
APP_OBJ  := $(APP_SRC:%.cpp=$(BUILD)/%.o)
TEST_OBJ := $(TEST_SRC:%.cpp=$(BUILD)/%.o)
TESTS    := $(TEST_NAMES:%=$(BUILD)/%)

ASAN_FLAGS := -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra

.PHONY: all test tests run clean asan tablescheck
all: $(GAME)

$(GAME): $(CORE_OBJ) $(UI_OBJ) $(APP_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(TESTS): $(BUILD)/%: $(BUILD)/tests/%.o $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

tests: $(TESTS)

test: $(TESTS)
	$(BUILD)/test_engine
	$(BUILD)/test_ai
	$(BUILD)/sim --hands 400 --seed 7
	$(BUILD)/sim --hands 400 --seed 11 --levels 2,0,1,2 --rotate
	$(BUILD)/sim --hands 200 --seed 13 --levels 2,1,2,1 --rotate --katlamali
	$(BUILD)/sim --hands 200 --seed 19 --levels 2,1,2,1 --rotate --esli
	$(BUILD)/sim --hands 100 --seed 17 --levels 2,1,0,2 --rotate --okey
	$(BUILD)/test_tavla
	$(BUILD)/tavla_sim --games 200 --seed 3 --levels 2,1
	$(BUILD)/test_batak
	$(BUILD)/batak_sim --hands 100 --seed 3 --levels 2,1,0,1
	$(BUILD)/batak_sim --hands 60 --seed 4 --levels 2,1,2,1 --esli
	$(BUILD)/test_king
	$(BUILD)/king_sim --games 4 --seed 3 --levels 2,1,0,1
	$(BUILD)/test_pisti
	$(BUILD)/pisti_sim --hands 200 --seed 3 --levels 2,1,0,1
	$(BUILD)/pisti_sim --hands 100 --seed 4 --levels 2,1,2,1 --mode esli

# The other table games played by the mouse in a hidden window (needs a display: run it awake, not over ssh)
TABLES_CHECK_OBJ := $(CORE_OBJ) $(UI_OBJ) $(filter-out $(BUILD)/src/app/App.o $(BUILD)/src/app/main.o,$(APP_OBJ))
$(BUILD)/tables_check: $(BUILD)/tools/tables_check.o $(TABLES_CHECK_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@
tablescheck: $(BUILD)/tables_check
	$(BUILD)/tables_check --hands 2

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

clean:
	rm -rf build/make build/asan $(GAME)

-include $(CORE_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
