# Beerlang Makefile
# Simple, transparent build system

CC = gcc

# Optional features — set on the command line, e.g.: make CFFI=1
CFFI ?= 0
ifeq ($(CFFI),1)
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    SDKROOT := $(shell xcrun --show-sdk-path 2>/dev/null)
    EXTRA_CFLAGS  += -I$(SDKROOT)/usr/include/ffi
    EXTRA_LDFLAGS += -lffi
  else
    EXTRA_CFLAGS  += $(shell pkg-config --cflags libffi 2>/dev/null)
    EXTRA_LDFLAGS += $(shell pkg-config --libs libffi 2>/dev/null || echo -lffi) -ldl
  endif
endif

CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O2 -fno-strict-aliasing -Iinclude -Ivendor -I. $(EXTRA_CFLAGS)
DEBUG_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG $(EXTRA_CFLAGS)
TRACK_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG -DBEER_TRACK_ALLOCS $(EXTRA_CFLAGS)
ASAN_CFLAGS = -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -g -O0 -Iinclude -Ivendor -I. -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer $(EXTRA_CFLAGS)
LDFLAGS = -lm -lpthread $(EXTRA_LDFLAGS)

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

# Runtime sources and feature switches, shared with wasm/ and embedders
# (see beerlang.mk): make BEER_NET=0, make BEER_PLATFORM=none, ...
BEER_CFFI := $(CFFI)
include beerlang.mk
EXTRA_CFLAGS += $(BEER_DEFS)

REPL_SRCS = $(wildcard $(SRC_DIR)/repl/*.c)
ALL_SRCS = $(BEER_SRCS) $(REPL_SRCS)

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
.PHONY: all clean test bench repl repl-trace debug track-leaks asan install uninstall libbeerlang embed wasm wasm-docker help

all: $(BIN_DIR)/beerlang

# Main executable
$(BIN_DIR)/beerlang: $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built beerlang executable"

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Vendor library object files
$(BUILD_DIR)/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d) $(VENDOR_OBJS:.o=.d)

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

# WASM REPL build (requires emcc on PATH)
wasm:
	$(MAKE) -C wasm -f Makefile.wasm

# WASM REPL build via Docker (no local emcc needed)
wasm-docker:
	docker run --rm \
	  -v "$(CURDIR):/src" \
	  -w /src/wasm \
	  --user "$(shell id -u):$(shell id -g)" \
	  emscripten/emsdk \
	  make -f Makefile.wasm

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

# Micro-benchmarks: prints ns/op per benchmark and the change vs the
# previous run. Results live in .bench/ (latest.edn, prev.edn), which
# `make clean` leaves alone.
#   make bench                    benchmark the working tree
#   make bench REF=<sha>          build <sha> in .bench/src and benchmark it
#                                 with the current benchmark set
# EXTRA_CFLAGS applies to either build, e.g. EXTRA_CFLAGS=-DVM_THREADED_DISPATCH
BENCH_DIR = .bench
ifdef REF
bench:
	@mkdir -p $(BENCH_DIR)
	rm -rf $(BENCH_DIR)/src && mkdir -p $(BENCH_DIR)/src
	git archive $(REF) | tar -x -C $(BENCH_DIR)/src
	$(MAKE) -C $(BENCH_DIR)/src all
	@echo "== benchmarking $(REF) $(EXTRA_CFLAGS)"
	BEERPATH=$(BENCH_DIR)/src/lib:lib $(BENCH_DIR)/src/$(BIN_DIR)/beerlang tests/integration/benchmarks/run.beer
else
bench: $(BIN_DIR)/beerlang
	@mkdir -p $(BENCH_DIR)
	@echo "== benchmarking working tree $(EXTRA_CFLAGS)"
	BEERPATH=lib ./$(BIN_DIR)/beerlang tests/integration/benchmarks/run.beer
endif

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

# Tools tar — rebuilt whenever tools.beer changes
lib/tools.tar: lib/tools/beer/tools.beer
	cd lib/tools && COPYFILE_DISABLE=1 tar cf ../tools.tar beer/tools.beer

# Install
# Installs the binary and standard library under PREFIX (default /usr/local).
#
#   beerlang   — REPL/script runner (wrapper that sets BEERPATH + execs binary)
#   beer       — project tool CLI (subcommand dispatch → beer.tools)
#
# Examples:
#   make install                       # → /usr/local
#   make install PREFIX=/opt/beerlang
#   make install PREFIX=$$HOME/.local
install: $(BIN_DIR)/beerlang lib/tools.tar
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
	@{ \
	  echo '#!/bin/sh'; \
	  echo '# beer — beerlang project tool'; \
	  echo '# Subcommands are implemented in beer.tools ($(SHAREDIR)/lib/tools/).'; \
	  echo '# Adding a directory earlier in BEERPATH overrides any bundled library.'; \
	  echo '_BEER_BIN="$(LIBEXECDIR)/beerlang"'; \
	  echo 'export BEERPATH="$(SHAREDIR)/lib"'; \
	  echo 'case "$$1" in'; \
	  echo '  new|run|build|ubertar)'; \
	  echo '    exec "$$_BEER_BIN" "$$@";;'; \
	  echo '  *)'; \
	  echo '    exec "$$_BEER_BIN" "$$@";;'; \
	  echo 'esac'; \
	} > $(BINDIR)/beer
	chmod 755 $(BINDIR)/beer
	install -m 755 scripts/beer-probe $(BINDIR)/beer-probe
	@echo ""
	@echo "  binary:     $(BINDIR)/beerlang"
	@echo "  beer:       $(BINDIR)/beer"
	@echo "  beer-probe: $(BINDIR)/beer-probe"
	@echo "  library:    $(SHAREDIR)/lib"
	@echo ""
	@echo "Make sure $(BINDIR) is in your PATH."

# Uninstall
uninstall:
	@echo "Removing beerlang from $(PREFIX)..."
	rm -f  $(BINDIR)/beerlang
	rm -f  $(BINDIR)/beer
	rm -f  $(BINDIR)/beer-probe
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
	@echo "  make wasm          Build WASM REPL (requires emcc on PATH)"
	@echo "  make wasm-docker   Build WASM REPL via Docker (no local emcc needed)"
	@echo "  make install       Install to PREFIX (default: /usr/local)"
	@echo "  make uninstall     Remove installed files"
	@echo "  make debug         Build with debug symbols"
	@echo "  make track-leaks   Build with allocation tracking (--dump-leaks)"
	@echo "  make asan          Build with AddressSanitizer"
	@echo "  make test          Run all tests"
	@echo "  make bench         Run micro-benchmarks (compares with the previous run)"
	@echo "  make bench REF=sha Benchmark another commit with the current benchmark set"
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
	@echo "Optional features (set on command line):"
	@echo "  CFFI=1    Enable C Foreign Function Interface via libffi (default: 0)"
	@echo ""
	@echo "Build variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
