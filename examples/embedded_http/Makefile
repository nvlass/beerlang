# examples/embedded_http/Makefile
#
# Builds a beerlang-embedded HTTP server on top of CivetWeb (MIT licensed,
# vendored in vendor/ -- see vendor/LICENSE.md). CivetWeb owns the socket
# and HTTP-parsing layer; routing and handler logic are written in
# beerlang (app.beer), called from C via libbeerlang (see main.c).
#
# Depends on:
#   - ../../build/libbeerlang.a  (run `make libbeerlang` in the repo root first)
#
# Usage:
#   make          — build ./embedded_http
#   make run      — build and run (sets BEERPATH)
#   make clean    — remove build artefacts

BEER_ROOT := ../..
BEER_INC  := $(BEER_ROOT)/include
BEER_LIB  := $(BEER_ROOT)/build

CC      := gcc
CFLAGS  := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -O2 -I$(BEER_INC) -Ivendor
LDFLAGS := -L$(BEER_LIB) -lbeerlang -lm -lpthread

# CivetWeb built with SSL disabled (no HTTPS needed for this example) --
# keeps the dependency footprint at zero beyond libc/pthread.
CIVET_CFLAGS := -std=c11 -Wall -Wno-unused-parameter -O2 -DNO_SSL -Ivendor

.PHONY: all run clean check-deps

all: check-deps embedded_http

check-deps:
	@test -f $(BEER_LIB)/libbeerlang.a || \
	  (echo "ERROR: $(BEER_LIB)/libbeerlang.a not found — run 'make libbeerlang' in $(BEER_ROOT) first" && exit 1)

civetweb.o: vendor/civetweb.c
	$(CC) $(CIVET_CFLAGS) -c vendor/civetweb.c -o civetweb.o

embedded_http: main.c civetweb.o
	$(CC) $(CFLAGS) -o embedded_http main.c civetweb.o $(LDFLAGS)
	@echo "Built: ./embedded_http"

run: embedded_http
	BEERPATH=$(BEER_ROOT)/lib ./embedded_http

clean:
	rm -f embedded_http civetweb.o
