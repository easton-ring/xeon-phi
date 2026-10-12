# Guest-side checks with the EL7 system toolchain (gcc 4.8.5, no CMake 3).   *** UNVERIFIED ***
# EL7.3 has CMake 2.8 and Python 2.7, so the top-level `make test` (CMake >= 3.13,
# Python >= 3.10) runs on the lab HOST, not here. This target builds the same
# C99 core and CTest test executables with plain gcc and runs them: the
# "old gcc" portability check.
#
#   make -C mic/guest check
# make predefines CC=cc, so `CC ?= ...` would never apply; override only the default.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c99 -O2 -fopenmp -ffp-contract=off -Wall -Wextra
ROOT   := ../..
OUT    ?= $(ROOT)/build-guest-gcc
CORE   := $(addprefix $(ROOT)/csrc/src/,crc32.c sha256.c timing.c kncds.c kernels.c fft.c features.c model.c)
TESTS  := test_hash test_kncds test_kernels test_fft test_features test_model
TOOLS  := knc_validate knc_bench knc_infer
GIT    := $(shell git -C $(ROOT) rev-parse HEAD 2>/dev/null || echo unknown)

.PHONY: check clean
check: $(addprefix $(OUT)/,$(TESTS) $(TOOLS))
	@$(CC) --version | head -1
	@cd $(OUT) && for t in $(TESTS); do printf '%-14s ' $$t; ./$$t || exit 1; done
	@echo "guest gcc check: all C tests passed"

$(OUT)/%: $(ROOT)/csrc/tests/%.c $(CORE)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(ROOT)/csrc/include -o $@ $^ -lm

$(OUT)/%: $(ROOT)/csrc/tools/%.c $(CORE)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I$(ROOT)/csrc/include -DKNC_GIT_COMMIT='"$(GIT)"' \
	    -DKNC_BUILD_FLAGS='"$(CC) $(CFLAGS)"' -o $@ $^ -lm

clean:
	rm -rf $(OUT)
