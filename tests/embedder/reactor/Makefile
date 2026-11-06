# The runtime built with BEER_PLATFORM=embedder, as beeros includes
# beerlang.mk, linked with the test's fake reactor.h. Run from the
# repository root through `make test-embedder` (part of `make test`).

BEER_PLATFORM := embedder
include beerlang.mk

CC ?= gcc
OUT := build/embedder
EMB_CFLAGS := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O1 -fno-strict-aliasing $(BEER_CFLAGS)
EMB_OBJS := $(patsubst %.c,$(OUT)/%.o,$(BEER_SRCS) $(BEER_VENDOR_SRCS))

bin/test_embedder_reactor: tests/embedder/reactor/test_embedder_reactor.c $(EMB_OBJS)
	@mkdir -p bin
	$(CC) $(EMB_CFLAGS) -o $@ $^ -lm -lpthread

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(EMB_CFLAGS) -MMD -MP -c $< -o $@

-include $(EMB_OBJS:.o=.d)
