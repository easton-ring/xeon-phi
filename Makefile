# knc-rf-lab: host pipeline. MIC builds live in mic/ and are never built here.
PYTHON ?= $(shell test -x .venv/bin/python && echo .venv/bin/python || echo python3)
BUILD  ?= build
DATA   ?= data
RESULTS ?= results
MODELS ?= artifacts/models
JOBS   ?= $(shell nproc 2>/dev/null || echo 2)

.PHONY: all build test ctest pytest data validate bench train evaluate infer-check demo portability clean

all: build

# Reconfigure every time so the git commit baked into the binaries is current.
build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release > /dev/null
	cmake --build $(BUILD) -j $(JOBS)

ctest: build
	cd $(BUILD) && ctest --output-on-failure

pytest: build
	KNC_BUILD_DIR=$(abspath $(BUILD)) $(PYTHON) -m pytest -q -rs

test: ctest pytest

data:
	$(PYTHON) -m knc_rf_lab generate --config configs/dev.yaml --out $(DATA)/dev
	$(PYTHON) -m knc_rf_lab generate --config configs/shifted.yaml --out $(DATA)/shifted

validate: build
	$(PYTHON) -m knc_rf_lab validate $(DATA)/dev/*.kncds $(DATA)/shifted/*.kncds
	$(BUILD)/knc_validate $(DATA)/dev/*.kncds $(DATA)/shifted/*.kncds

bench: build
	KNC_BUILD_DIR=$(abspath $(BUILD)) $(PYTHON) -m knc_rf_lab.benchmark --dataset $(DATA)/dev/test.kncds \
	    --batches 1,64,1024 --reps 10 --out $(RESULTS)/bench-host.json --plot

train:
	$(PYTHON) -m knc_rf_lab.classify train --data $(DATA)/dev --out $(MODELS)

evaluate:
	$(PYTHON) -m knc_rf_lab.classify evaluate --data $(DATA)/dev --models $(MODELS) \
	    --shifted $(DATA)/shifted/test.kncds --out $(RESULTS)/eval

infer-check: build
	KNC_BUILD_DIR=$(abspath $(BUILD)) $(PYTHON) -m knc_rf_lab.classify infer_check --data $(DATA)/dev \
	    --models $(MODELS) --out $(RESULTS)/eval

# generate -> validate (Python and C) -> benchmark -> train -> evaluate -> C inference check
demo: build
	$(MAKE) --no-print-directory data
	$(MAKE) --no-print-directory validate
	$(MAKE) --no-print-directory bench
	$(MAKE) --no-print-directory train
	$(MAKE) --no-print-directory evaluate
	$(MAKE) --no-print-directory infer-check
	@echo "demo complete: $(RESULTS)/bench-host.json, $(RESULTS)/eval/metrics.json, $(RESULTS)/eval/infer_check.json"

# Strict C99 compile with every available C compiler (no OpenMP, -Werror), then a
# full no-OpenMP, non-native CMake build + CTest: stands in for "old compiler"
# portability until icc -mmic is available (docs/status.md).
portability:
	@for cc in gcc clang; do command -v $$cc >/dev/null || continue; \
	  for f in csrc/src/*.c csrc/tools/*.c; do \
	    $$cc -std=c99 -pedantic -Wall -Wextra -Werror -Wno-unknown-pragmas -O2 -Icsrc/include -c $$f -o /dev/null || exit 1; \
	  done; echo "$$cc: strict C99 OK"; done
	cmake -S . -B $(BUILD)-portable -DKNC_OPENMP=OFF -DKNC_NATIVE=OFF -DCMAKE_BUILD_TYPE=Release > /dev/null
	cmake --build $(BUILD)-portable -j $(JOBS)
	cd $(BUILD)-portable && ctest --output-on-failure
	$(MAKE) -s -C mic/offload syntax-check && echo "mic/offload: C syntax OK (pragmas not checked)"
	$(MAKE) -s -C mic/offload emit-test OUT=$(abspath $(BUILD)-portable) && echo "mic/offload: argument parser + JSON emitter build OK (test mode, no offload code)"
	sh mic/toolchain/selftest.sh $(abspath $(BUILD)-portable)/toolchain-selftest > /dev/null && echo "mic/toolchain: selftest OK"

clean:
	rm -rf $(BUILD)-portable $(BUILD) $(DATA) $(RESULTS)/bench-host.* $(RESULTS)/eval artifacts
