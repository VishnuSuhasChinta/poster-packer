# Poster Packer - plain Makefile (MinGW g++ 6.3 -> C++14). Run with: mingw32-make
CXX      := g++
CXXFLAGS := -std=gnu++14 -O2 -Wall -Wextra -Wpedantic
GDIPLUS  := -lgdiplus -lgdi32 -lole32

CORE_SRC := $(wildcard src/core/*.cpp)
IO_SRC   := $(wildcard src/io/*.cpp)
PLAT_SRC := $(wildcard src/platform/*.cpp)

OBJ_DIR  := build
CORE_OBJ := $(CORE_SRC:src/core/%.cpp=$(OBJ_DIR)/core_%.o)
IO_OBJ   := $(IO_SRC:src/io/%.cpp=$(OBJ_DIR)/io_%.o)
PLAT_OBJ := $(PLAT_SRC:src/platform/%.cpp=$(OBJ_DIR)/plat_%.o)
HEADERS  := $(wildcard src/*/*.h)

.PHONY: all test bench clean
all: $(OBJ_DIR)/posterpacker_cli.exe $(OBJ_DIR)/make_test_images.exe $(OBJ_DIR)/posterpacker.exe

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/core_%.o: src/core/%.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ_DIR)/io_%.o: src/io/%.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ_DIR)/plat_%.o: src/platform/%.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/packer_harness.exe: tests/packer_harness.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@
$(OBJ_DIR)/optimizer_harness.exe: tests/optimizer_harness.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@
$(OBJ_DIR)/benchmark.exe: tests/benchmark.cpp $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@
$(OBJ_DIR)/posterpacker_cli.exe: tools/posterpacker_cli.cpp $(CORE_OBJ) $(IO_OBJ) $(PLAT_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GDIPLUS)
$(OBJ_DIR)/make_test_images.exe: tools/make_test_images.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GDIPLUS)
$(OBJ_DIR)/posterpacker.exe: gui/main.cpp $(CORE_OBJ) $(IO_OBJ) $(PLAT_OBJ)
	$(CXX) $(CXXFLAGS) -mwindows $^ -o $@ $(GDIPLUS) -lcomctl32 -lcomdlg32 -lshell32

test: $(OBJ_DIR)/packer_harness.exe $(OBJ_DIR)/optimizer_harness.exe
	./$(OBJ_DIR)/packer_harness.exe
	./$(OBJ_DIR)/optimizer_harness.exe

bench: $(OBJ_DIR)/benchmark.exe
	./$(OBJ_DIR)/benchmark.exe 1.0 8

clean:
	rm -rf $(OBJ_DIR)
