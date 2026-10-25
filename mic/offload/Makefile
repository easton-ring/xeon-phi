# Offload benchmark build.                                      *** UNVERIFIED ***
# Not part of the default host build. Run in the MPSS guest with the Intel
# compiler (offload support is on by default in icc 13-17; -qno-offload disables it).
#
#   make -C mic/offload           -> ../../build-mic/offload_magsq
#   make -C mic/offload syntax-check   (gcc -fsyntax-only with a stub header; works anywhere,
#                                       checks C syntax only, NOT the offload pragmas)
# make predefines CC=cc, so `CC ?= ...` would never apply; override only the default.
ifeq ($(origin CC),default)
CC := icc
endif
FLAGS    ?= -qopenmp -O3 -std=c99 -fp-model precise
ROOT     := ../..
OUT      ?= $(ROOT)/build-mic
GIT      := $(shell git -C $(ROOT) rev-parse HEAD 2>/dev/null || echo unknown)$(shell test -n "$$(git -C $(ROOT) status --porcelain --untracked-files=no 2>/dev/null)" && echo -dirty)
DEFS     := -DKNC_GIT_COMMIT='"$(GIT)"' -DKNC_BUILD_FLAGS='"$(CC) $(FLAGS)"'

.PHONY: all syntax-check emit-test clean
all: $(OUT)/offload_magsq

$(OUT)/offload_magsq: offload_magsq.c
	mkdir -p $(OUT)
	$(CC) $(FLAGS) $(DEFS) -o $@ $< -lm

syntax-check:
	gcc -std=c99 -fopenmp -fsyntax-only -Wall -Wno-unknown-pragmas -D__INTEL_OFFLOAD \
	    -D__INTEL_COMPILER=1700 -DKNC_SYNTAX_CHECK -Isyntax-stub offload_magsq.c

# Test-only build of the argument parser + JSON emitter with gcc (no offload code).
emit-test:
	mkdir -p $(OUT)
	gcc -std=c99 -O1 -Wall -Wextra -Werror -DKNC_OFFLOAD_EMIT_TEST -o $(OUT)/offload_magsq_emit_test offload_magsq.c -lm

clean:
	rm -f $(OUT)/offload_magsq
