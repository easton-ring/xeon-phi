# KNC native build of the portable C99 core and tools.         *** UNVERIFIED ***
#
# Two toolchain profiles (TOOLCHAIN=icc is the default):
#
#   make -C mic/native                       Intel C++ 17 (Parallel Studio XE 2017), -mmic
#   make -C mic/native TOOLCHAIN=gcc-k1om    GCC cross-compiler shipped with MPSS
#
# icc: load the compiler environment first, e.g.
#   source <icc install>/bin/compilervars.sh intel64     then check: icc -V
#   (Composer XE 2013 SP1: /opt/intel/composer_xe_2013_sp1/bin/compilervars.sh per
#    Intel's native-build article; the icc 17 prefix differs - UNVERIFIED)
#   Flags (docs/mpss-compatibility.md has sources and verification status):
#     -mmic          generate code for the KNC coprocessor (k1om)
#     -qopenmp       OpenMP (icc >= 15 spelling; older icc used -openmp)
#     -fp-model precise   no value-unsafe FP optimizations (icc defaults to fast=1);
#                         required for the NaN/Inf tests in docs/kernels.md
#
# gcc-k1om: the k1om GCC cross-compiler from MPSS (forum-sourced location
#   /usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc; run mic/toolchain/probe-native.sh
#   first). It reportedly does NOT generate code for the 512-bit vector unit (IMCI),
#   so results are scalar KNC results; every record says so in metadata.toolchain.
#   It may not define __MIC__, so instead of the compile-time guard this profile
#   rejects any linked binary that is not K1OM (both profiles run the ELF check).
#
# The same sources build the host baseline with gcc (top-level CMakeLists.txt).

TOOLCHAIN ?= icc
ROOT      := ../..
OUT       ?= $(ROOT)/build-mic
ELFCHECK  := sh $(ROOT)/mic/toolchain/elf-target.sh

ifeq ($(TOOLCHAIN),icc)
# make predefines CC=cc, so `CC ?= ...` would never apply; override only the default.
ifeq ($(origin CC),default)
CC := icc
endif
MICFLAGS  ?= -mmic -qopenmp -O3 -std=c99 -fp-model precise
TC_DEFS   := -DKNC_TOOLCHAIN='"icc-mmic"' \
             -DKNC_VECTOR_ISA='"IMCI via icc auto-vectorization (not verified per kernel)"'
else ifeq ($(TOOLCHAIN),gcc-k1om)
ifeq ($(origin CC),default)
CC := $(firstword $(wildcard /usr/linux-k1om-*/bin/x86_64-k1om-linux-gcc) \
        $(shell command -v x86_64-k1om-linux-gcc k1om-mpss-linux-gcc 2>/dev/null))
endif
MICFLAGS  ?= -O3 -std=c99 -fopenmp -ffp-contract=off -fno-fast-math
TC_DEFS   := -DKNC_TOOLCHAIN_GCC_K1OM -DKNC_TOOLCHAIN='"gcc-k1om"' \
             -DKNC_VECTOR_ISA='"none expected: gcc k1om reportedly lacks IMCI (check probe VECTOR_VERDICT)"'
else
$(error TOOLCHAIN must be icc or gcc-k1om, got '$(TOOLCHAIN)')
endif

GIT       := $(shell git -C $(ROOT) rev-parse HEAD 2>/dev/null || echo unknown)$(shell test -n "$$(git -C $(ROOT) status --porcelain --untracked-files=no 2>/dev/null)" && echo -dirty)
CORE      := crc32 sha256 timing kncds kernels fft features model
CORE_SRC  := $(addprefix $(ROOT)/csrc/src/,$(addsuffix .c,$(CORE)))
DEFS      := -DKNC_GIT_COMMIT='"$(GIT)"' -DKNC_BUILD_FLAGS='"$(CC) $(MICFLAGS)"' \
             -DKNC_EXEC_MODE='"mic-native"' -DKNC_REQUIRE_MIC $(TC_DEFS)

.PHONY: all clean
all: $(OUT)/knc_bench $(OUT)/knc_validate $(OUT)/knc_infer

$(OUT):
	mkdir -p $@

# Link, then refuse to keep anything that is not a K1OM ELF executable.
$(OUT)/%: $(ROOT)/csrc/tools/%.c $(CORE_SRC) | $(OUT)
	@test -n "$(CC)" || { echo "no compiler for TOOLCHAIN=$(TOOLCHAIN); set CC=" >&2; exit 1; }
	$(CC) $(MICFLAGS) $(DEFS) -I$(ROOT)/csrc/include -o $@ $< $(CORE_SRC) -lm
	@t=$$($(ELFCHECK) $@); if [ "$$t" != K1OM ]; then rm -f $@; \
	  echo "ERROR: $@ is $$t, not a K1OM executable; removed (TOOLCHAIN=$(TOOLCHAIN), CC=$(CC))" >&2; exit 1; fi; \
	  echo "$@: K1OM ELF ok"

clean:
	rm -rf $(OUT)
