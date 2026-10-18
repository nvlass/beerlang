# beerlang.mk — the beerlang runtime's sources and flags, for any build that
# compiles them: the host Makefile, wasm/Makefile.wasm, embedders (beeros).
#
#   BEER_ROOT := path/to/beerlang      # before including; default: this dir
#   BEER_PLATFORM := none              # optional switches, see below
#   include $(BEER_ROOT)/beerlang.mk
#   ... compile $(BEER_SRCS) with $(BEER_CFLAGS)
#
# Switches (set before including, or on the command line):
#   BEER_PLATFORM  posix  kqueue/epoll I/O reactor (default)
#                  none   no fd readiness (WASM, bare metal): a stub reactor
#   BEER_NET     1/0  beer.tcp, beer.udp natives    (posix only)
#   BEER_SHELL   1/0  beer.shell/exec               (posix only)
#   BEER_CRYPTO  1/0  beer.crypto natives
#   BEER_AOT     1/0  .beerc compile/load (beer.beerc)
#   BEER_CFFI    1/0  beer.ffi via libffi (link libffi yourself), default 0
# A switched-off feature adds -DBEER_NO_<FEATURE>, so namespace_init doesn't
# register its natives. Nothing defined means everything on.
#
# Defines:
#   BEER_SRCS        runtime sources (no main(): add src/repl/main.c for the CLI)
#   BEER_VENDOR_SRCS mini-gmp, ulog
#   BEER_CFLAGS      include paths and feature defines

# Captured now: MAKEFILE_LIST grows as more makefiles are read
ifndef BEER_ROOT
  BEER_ROOT := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
endif

BEER_PLATFORM ?= posix
ifeq ($(BEER_PLATFORM),none)
  BEER_NET   ?= 0
  BEER_SHELL ?= 0
else
  BEER_NET   ?= 1
  BEER_SHELL ?= 1
endif
BEER_CRYPTO ?= 1
BEER_AOT    ?= 1
BEER_CFFI   ?= 0

# "src/..." rather than "./src/..." when included from the beerlang tree
_BEER_PREFIX := $(if $(filter .,$(BEER_ROOT)),,$(BEER_ROOT)/)
_BEER_SRC := $(_BEER_PREFIX)src

# Feature files, kept out of the core list below
_BEER_PLATFORM_POSIX := $(_BEER_SRC)/io/reactor.c $(_BEER_SRC)/io/io_reactor.c
_BEER_PLATFORM_NONE  := $(_BEER_SRC)/platform/none/io_reactor_none.c
_BEER_NET    := $(_BEER_SRC)/runtime/tcp.c $(_BEER_SRC)/runtime/udp.c
_BEER_SHELL  := $(_BEER_SRC)/runtime/shell.c
_BEER_CRYPTO := $(_BEER_SRC)/runtime/crypto.c
_BEER_AOT    := $(_BEER_SRC)/lib/aot.c

# Everything else is core: a new source file is picked up by every build
_BEER_CORE := $(filter-out $(_BEER_PLATFORM_POSIX) $(_BEER_NET) $(_BEER_SHELL) $(_BEER_CRYPTO) $(_BEER_AOT), \
  $(wildcard $(_BEER_SRC)/vm/*.c) \
  $(wildcard $(_BEER_SRC)/types/*.c) \
  $(wildcard $(_BEER_SRC)/memory/*.c) \
  $(wildcard $(_BEER_SRC)/reader/*.c) \
  $(wildcard $(_BEER_SRC)/compiler/*.c) \
  $(wildcard $(_BEER_SRC)/runtime/*.c) \
  $(wildcard $(_BEER_SRC)/scheduler/*.c) \
  $(wildcard $(_BEER_SRC)/task/*.c) \
  $(wildcard $(_BEER_SRC)/channel/*.c) \
  $(wildcard $(_BEER_SRC)/io/*.c) \
  $(wildcard $(_BEER_SRC)/lib/*.c))

BEER_SRCS := $(_BEER_CORE)
BEER_DEFS :=

ifeq ($(BEER_PLATFORM),none)
  BEER_SRCS += $(_BEER_PLATFORM_NONE)
  BEER_DEFS += -DBEER_PLATFORM_NONE
else ifeq ($(BEER_PLATFORM),posix)
  BEER_SRCS += $(_BEER_PLATFORM_POSIX)
else
  $(error BEER_PLATFORM must be posix or none, not '$(BEER_PLATFORM)')
endif

ifneq ($(BEER_PLATFORM),posix)
  ifeq ($(BEER_NET),1)
    $(error BEER_NET=1 needs BEER_PLATFORM=posix)
  endif
  ifeq ($(BEER_SHELL),1)
    $(error BEER_SHELL=1 needs BEER_PLATFORM=posix)
  endif
endif

ifeq ($(BEER_NET),1)
  BEER_SRCS += $(_BEER_NET)
else
  BEER_DEFS += -DBEER_NO_NET
endif
ifeq ($(BEER_SHELL),1)
  BEER_SRCS += $(_BEER_SHELL)
else
  BEER_DEFS += -DBEER_NO_SHELL
endif
ifeq ($(BEER_CRYPTO),1)
  BEER_SRCS += $(_BEER_CRYPTO)
else
  BEER_DEFS += -DBEER_NO_CRYPTO
endif
ifeq ($(BEER_AOT),1)
  BEER_SRCS += $(_BEER_AOT)
else
  BEER_DEFS += -DBEER_NO_AOT
endif
ifeq ($(BEER_CFFI),1)
  BEER_SRCS += $(wildcard $(_BEER_SRC)/cffi/*.c)
  BEER_DEFS += -DBEER_CFFI
endif

BEER_VENDOR_SRCS := $(_BEER_PREFIX)vendor/mini-gmp.c $(_BEER_PREFIX)vendor/ulog.c
BEER_CFLAGS := -I$(_BEER_PREFIX)include -I$(_BEER_PREFIX)vendor -I$(BEER_ROOT) $(BEER_DEFS)
