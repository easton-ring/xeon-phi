# KNC native build of the portable C99 core and tools.         *** UNVERIFIED ***
#
# Run inside the MPSS guest with the Intel compiler environment loaded, e.g.
#   source /opt/intel/compilers_and_libraries/linux/bin/compilervars.sh intel64   (path UNVERIFIED)
#   make -C mic/native
#
# Flags (see docs/mpss-compatibility.md for sources and verification status):
#   -mmic          generate code for the KNC coprocessor (k1om)
#   -qopenmp       OpenMP (icc >= 15 spelling; older icc used -openmp)
#   -fp-model precise   disable value-unsafe FP optimizations (icc defaults to fast=1);
#                       required for the NaN/Inf tests in docs/kernels.md
#   -std=c99
# The same sources build the host baseline with gcc (top-level CMakeLists.txt).

# make predefines CC=cc, so `CC ?= ...` would never apply; override only the default.
ifeq ($(origin CC),default)
CC := icc
endif
MICFLAGS  ?= -mmic -qopenmp -O3 -std=c99 -fp-model precise
ROOT      := ../..
OUT       ?= $(ROOT)/build-mic
GIT       := $(shell git -C $(ROOT) rev-parse HEAD 2>/dev/null || echo unknown)$(shell test -n "$$(git -C $(ROOT) status --porcelain --untracked-files=no 2>/dev/null)" && echo -dirty)
CORE      := crc32 sha256 timing kncds kernels fft features model
CORE_SRC  := $(addprefix $(ROOT)/csrc/src/,$(addsuffix .c,$(CORE)))
DEFS      := -DKNC_GIT_COMMIT='"$(GIT)"' -DKNC_BUILD_FLAGS='"$(CC) $(MICFLAGS)"' \
             -DKNC_EXEC_MODE='"mic-native"' -DKNC_REQUIRE_MIC

.PHONY: all clean
all: $(OUT)/knc_bench $(OUT)/knc_validate $(OUT)/knc_infer

$(OUT):
	mkdir -p $@

$(OUT)/%: $(ROOT)/csrc/tools/%.c $(CORE_SRC) | $(OUT)
	$(CC) $(MICFLAGS) $(DEFS) -I$(ROOT)/csrc/include -o $@ $< $(CORE_SRC) -lm

clean:
	rm -rf $(OUT)
