CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g -D_GNU_SOURCE
LDFLAGS = -lpthread -lm

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BENCH_DIR = bench
BUILD_DIR = build
BIN_DIR = bin

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c, $(SRCS))
LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
MAIN_OBJ = $(BUILD_DIR)/main.o

# Test sources
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/tests/%.o)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(BIN_DIR)/%)

# Bench sources
BENCH_SRCS = $(wildcard $(BENCH_DIR)/*.c)
BENCH_OBJS = $(BENCH_SRCS:$(BENCH_DIR)/%.c=$(BUILD_DIR)/bench/%.o)
BENCH_BINS = $(BENCH_SRCS:$(BENCH_DIR)/%.c=$(BIN_DIR)/%)

# Targets
TARGET = $(BIN_DIR)/calderadb

# Includes
INCLUDES = -I$(INCLUDE_DIR)

.PHONY: all clean test bench directories debug valgrind

all: directories $(TARGET)

directories:
	mkdir -p $(BUILD_DIR) $(BUILD_DIR)/tests $(BUILD_DIR)/bench $(BIN_DIR)

$(TARGET): $(LIB_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -c $< -o $@

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -c $< -o $@

$(BIN_DIR)/test%: $(BUILD_DIR)/tests/test%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/bench/%.o: $(BENCH_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -c $< -o $@

$(BIN_DIR)/bench%: $(BUILD_DIR)/bench/bench%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: directories $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "Running $$t"; ./$$t; done

bench: directories $(BENCH_BINS)
	@for b in $(BENCH_BINS); do echo "Running $$b"; ./$$b; done

debug: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all test bench

valgrind: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "Valgrinding $$t"; \
		valgrind --leak-check=full --error-exitcode=1 ./$$t || exit 1; \
	done

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJS:.o=.d) $(BENCH_OBJS:.o=.d)