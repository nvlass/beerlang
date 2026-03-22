# Beerlang Makefile
# Simple, transparent build system

CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O2 -fno-strict-aliasing -Iinclude -Ilib -I.
DEBUG_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ilib -I. -DDEBUG
TRACK_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ilib -I. -DDEBUG -DBEER_TRACK_ALLOCS
ASAN_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ilib -I. -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS = -lm -lpthread

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

# Source files
VM_SRCS = $(wildcard $(SRC_DIR)/vm/*.c)
TYPES_SRCS = $(wildcard $(SRC_DIR)/types/*.c)
MEMORY_SRCS = $(wildcard $(SRC_DIR)/memory/*.c)
READER_SRCS = $(wildcard $(SRC_DIR)/reader/*.c)
COMPILER_SRCS = $(wildcard $(SRC_DIR)/compiler/*.c)
RUNTIME_SRCS = $(wildcard $(SRC_DIR)/runtime/*.c)
SCHEDULER_SRCS = $(wildcard $(SRC_DIR)/scheduler/*.c)
TASK_SRCS = $(wildcard $(SRC_DIR)/task/*.c)
CHANNEL_SRCS = $(wildcard $(SRC_DIR)/channel/*.c)
IO_SRCS = $(wildcard $(SRC_DIR)/io/*.c)
REPL_SRCS = $(wildcard $(SRC_DIR)/repl/*.c)
LIB_SRCS = lib/mini-gmp.c lib/ulog.c

ALL_SRCS = $(VM_SRCS) $(TYPES_SRCS) $(MEMORY_SRCS) $(READER_SRCS) \
           $(COMPILER_SRCS) $(RUNTIME_SRCS) $(SCHEDULER_SRCS) \
           $(TASK_SRCS) $(CHANNEL_SRCS) $(IO_SRCS) $(REPL_SRCS)

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(ALL_SRCS))
LIB_OBJS = $(BUILD_DIR)/lib/mini-gmp.o $(BUILD_DIR)/lib/ulog.o

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%,$(TEST_SRCS))

# Targets
.PHONY: all clean test repl repl-trace debug track-leaks asan help

all: $(BIN_DIR)/beerlang

# Main executable
$(BIN_DIR)/beerlang: $(OBJS) $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built beerlang executable"

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Library object files
$(BUILD_DIR)/lib/%.o: lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: clean all
	@echo "Built debug version"

# Build with allocation tracking (enables --dump-leaks flag)
track-leaks: CFLAGS = $(TRACK_CFLAGS)
track-leaks: clean all
	@echo "Built with allocation tracking (use --dump-leaks)"

# ASAN build (AddressSanitizer + UndefinedBehaviorSanitizer)
asan: CFLAGS = $(ASAN_CFLAGS)
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all
	@echo "Built with AddressSanitizer (run normally to detect memory errors)"

# REPL (depends on main executable)
repl: $(BIN_DIR)/beerlang
	./$(BIN_DIR)/beerlang

repl-trace: $(BIN_DIR)/beerlang
	./$(BIN_DIR)/beerlang --trace

# Tests
test: $(TEST_BINS)
	@echo "Running tests..."
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done
	@echo "All tests passed!"

# Build individual test
$(BIN_DIR)/test_%: $(TEST_DIR)/%.c $(OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(filter-out $(BUILD_DIR)/repl/main.o,$(OBJS)) $(LIB_OBJS) $(LDFLAGS)

# Tools tar
lib/tools.tar: lib/tools/beer/tools.beer
	cd lib/tools && COPYFILE_DISABLE=1 tar cf ../tools.tar beer/tools.beer

# Clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

# Help
help:
	@echo "Beerlang Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build beerlang (default)"
	@echo "  make debug    - Build with debug symbols"
	@echo "  make track-leaks - Build with allocation tracking (--dump-leaks)"
	@echo "  make test     - Run all tests"
	@echo "  make repl     - Start REPL"
	@echo "  make repl-trace - Start REPL with opcode tracing"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
