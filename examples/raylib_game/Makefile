# examples/raylib_game/Makefile
#
# Builds a standalone beerlang + raylib game.
# Depends on:
#   - ../../build/libbeerlang.a  (run `make` in the repo root first)
#   - raylib installed on the system
#
# Install raylib:
#   macOS:  brew install raylib
#   Linux:  apt install libraylib-dev   (or build from source)
#
# Usage:
#   make          — build ./game
#   make run      — build and run
#   make clean    — remove build artefacts

BEER_ROOT := ../..
BEER_INC  := $(BEER_ROOT)/include
BEER_LIB  := $(BEER_ROOT)/build

CC     := gcc
CFLAGS := -std=c11 -Wall -Wextra -O2 -I$(BEER_INC)

# ── raylib detection ──────────────────────────────────────────────────────────
# Override with: make RAYLIB_PREFIX=/path/to/raylib
RAYLIB_PREFIX ?= $(shell brew --prefix raylib 2>/dev/null || echo /usr/local)
RAYLIB_INC    := $(RAYLIB_PREFIX)/include
RAYLIB_LIB    := $(RAYLIB_PREFIX)/lib

CFLAGS  += -I$(RAYLIB_INC)
LDFLAGS := -L$(BEER_LIB) -lbeerlang -L$(RAYLIB_LIB) -lraylib -lm -lpthread

# macOS needs window/graphics frameworks
ifeq ($(shell uname -s), Darwin)
    LDFLAGS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
endif

# ── targets ───────────────────────────────────────────────────────────────────
.PHONY: all run clean check-deps

all: check-deps game

check-deps:
	@test -f $(BEER_LIB)/libbeerlang.a || \
	  (echo "ERROR: $(BEER_LIB)/libbeerlang.a not found — run 'make libbeerlang' in $(BEER_ROOT) first" && exit 1)
	@test -f $(RAYLIB_INC)/raylib.h || \
	  (echo "ERROR: raylib.h not found — install raylib (brew install raylib / apt install libraylib-dev)" && exit 1)

game: main.c
	$(CC) $(CFLAGS) -o game main.c $(LDFLAGS)
	@echo "Built: ./game"

run: game
	BEERPATH=$(BEER_ROOT)/lib ./game

clean:
	rm -f game
