# Beerlang Makefile
# Simple, transparent build system

CC = gcc
CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O2 -fno-strict-aliasing -Iinclude -Ivendor -I.
DEBUG_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG
TRACK_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG -DBEER_TRACK_ALLOCS
ASAN_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
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
LIB_SRCS = $(wildcard $(SRC_DIR)/lib/*.c)
VENDOR_SRCS = vendor/mini-gmp.c vendor/ulog.c

ALL_SRCS = $(VM_SRCS) $(TYPES_SRCS) $(MEMORY_SRCS) $(READER_SRCS) \
           $(COMPILER_SRCS) $(RUNTIME_SRCS) $(SCHEDULER_SRCS) \
           $(TASK_SRCS) $(CHANNEL_SRCS) $(IO_SRCS) $(REPL_SRCS) $(LIB_SRCS)

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(ALL_SRCS))
VENDOR_OBJS = $(BUILD_DIR)/vendor/mini-gmp.o $(BUILD_DIR)/vendor/ulog.o

# Test files
TEST_SRCS = $(wildcard $(TEST_DIR)/*/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%,$(TEST_SRCS))

# Install paths — override with: make install PREFIX=~/.local
PREFIX     ?= /usr/local
BINDIR      = $(PREFIX)/bin
LIBEXECDIR  = $(PREFIX)/lib/beerlang
SHAREDIR    = $(PREFIX)/share/beerlang

# Targets
.PHONY: all clean test repl repl-trace debug track-leaks asan install uninstall libbeerlang embed help

all: $(BIN_DIR)/beerlang

# Main executable
$(BIN_DIR)/beerlang: $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built beerlang executable"

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Vendor library object files
$(BUILD_DIR)/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Static library for embedding
LIB_OBJS = $(filter-out $(BUILD_DIR)/repl/main.o, $(OBJS)) $(VENDOR_OBJS)

libbeerlang: $(LIB_OBJS)
	ar rcs $(BUILD_DIR)/libbeerlang.a $^
	@echo "Built $(BUILD_DIR)/libbeerlang.a"

# Embed example (depends on libbeerlang)
embed: libbeerlang
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN_DIR)/embed examples/embed.c \
		-L$(BUILD_DIR) -lbeerlang $(LDFLAGS)
	@echo "Built $(BIN_DIR)/embed"

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
$(BIN_DIR)/test_%: $(TEST_DIR)/%.c $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(filter-out $(BUILD_DIR)/repl/main.o,$(OBJS)) $(VENDOR_OBJS) $(LDFLAGS)

# Tools tar
lib/tools.tar: lib/tools/beer/tools.beer
	cd lib/tools && COPYFILE_DISABLE=1 tar cf ../tools.tar beer/tools.beer

# Install
# Installs the binary and standard library under PREFIX (default /usr/local).
# The real binary goes to $(LIBEXECDIR)/beerlang; a generated wrapper at
# $(BINDIR)/beerlang sets BEERPATH and exec's it — no manual env setup needed.
#
# Examples:
#   make install                       # → /usr/local
#   make install PREFIX=/opt/beerlang
#   make install PREFIX=$$HOME/.local
install: $(BIN_DIR)/beerlang
	@echo "Installing beerlang to $(PREFIX)..."
	install -d $(BINDIR) $(LIBEXECDIR) $(SHAREDIR)/lib
	install -m 755 $(BIN_DIR)/beerlang $(LIBEXECDIR)/beerlang
	cp -r lib/. $(SHAREDIR)/lib/
	@{ \
	  echo '#!/bin/sh'; \
	  echo 'export BEERPATH="$(SHAREDIR)/lib"'; \
	  echo 'exec "$(LIBEXECDIR)/beerlang" "$$@"'; \
	} > $(BINDIR)/beerlang
	chmod 755 $(BINDIR)/beerlang
	@echo ""
	@echo "  binary:  $(BINDIR)/beerlang"
	@echo "  library: $(SHAREDIR)/lib"
	@echo ""
	@echo "Make sure $(BINDIR) is in your PATH."

# Uninstall
uninstall:
	@echo "Removing beerlang from $(PREFIX)..."
	rm -f  $(BINDIR)/beerlang
	rm -rf $(LIBEXECDIR)
	rm -rf $(SHAREDIR)
	@echo "Done."

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
	@echo "  make               Build beerlang (default)"
	@echo "  make libbeerlang   Build build/libbeerlang.a (embeddable library)"
	@echo "  make embed         Build examples/embed.c against libbeerlang"
	@echo "  make install       Install to PREFIX (default: /usr/local)"
	@echo "  make uninstall     Remove installed files"
	@echo "  make debug         Build with debug symbols"
	@echo "  make track-leaks   Build with allocation tracking (--dump-leaks)"
	@echo "  make asan          Build with AddressSanitizer"
	@echo "  make test          Run all tests"
	@echo "  make repl          Start REPL"
	@echo "  make repl-trace    Start REPL with opcode tracing"
	@echo "  make clean         Remove build artifacts"
	@echo "  make help          Show this help"
	@echo ""
	@echo "Install variables (override on command line):"
	@echo "  PREFIX=$(PREFIX)"
	@echo "  BINDIR=$(BINDIR)"
	@echo "  LIBEXECDIR=$(LIBEXECDIR)"
	@echo "  SHAREDIR=$(SHAREDIR)"
	@echo ""
	@echo "Examples:"
	@echo "  make install PREFIX=/opt/beerlang"
	@echo "  make install PREFIX=\$$HOME/.local"
	@echo ""
	@echo "Build variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
