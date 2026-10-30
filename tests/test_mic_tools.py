"""Host-side tests for the MIC tooling (no card, no Intel compiler needed).

They check the parts that can be checked here: argument parsing and JSON of the
offload benchmark (test-only build), the toolchain verdict logic, the
gcc-k1om build profile's ELF guard, deploy/sweep failure handling.
"""

import json
import os
import shutil
import subprocess

import pytest

from conftest import ROOT
from knc_rf_lab.cbin import find_binary
from knc_rf_lab.schema import validate

needs_gcc = pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not installed")
needs_make = pytest.mark.skipif(shutil.which("make") is None, reason="make not installed")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


@pytest.fixture(scope="module")
def emit_test(tmp_path_factory):
    out = tmp_path_factory.mktemp("emit")
    p = run(["make", "-s", "-C", str(ROOT / "mic/offload"), "emit-test", f"OUT={out}"])
    if p.returncode != 0:
        pytest.skip(f"emit-test build failed: {p.stderr}")
    return out / "offload_magsq_emit_test"


@needs_gcc
@needs_make
def test_offload_records_are_schema_valid(emit_test):
    p = run([str(emit_test), "--n-examples", "8", "--samples", "16", "--reps", "5", "--chunks", "4",
             "--threads", "60"])
    assert p.returncode == 0, p.stderr
    recs = [json.loads(line) for line in p.stdout.splitlines()]
    assert [r["offload"]["pattern"] for r in recs] == ["resident", "per_call", "chunked"]
    for r in recs:
        validate(r, sub="ok_record")
        assert r["execution_mode"] == "mic-offload" and r["simulated"] is False
        assert r["error"]["max_abs"] is not None and r["error"]["examples_checked"] == 8
        assert r["offload"]["requested_threads"] == 60 and r["offload"]["mic_team_size"] == 60
        assert r["metadata"]["timestamp_utc"].endswith("Z")
        assert r["metadata"]["os"]["sysname"]
        # raw samples keep measurement order (the emitter must not sort them in place)
        assert r["timing"]["kernel"]["samples"] == [0.005, 0.004, 0.003, 0.002, 0.001]
        assert r["timing"]["kernel"]["median"] == pytest.approx(0.003)
    assert recs[0]["timing"]["transfer"] is not None and recs[0]["offload"]["kernel_includes_transfer"] is False
    assert recs[1]["timing"]["transfer"] is None and recs[1]["offload"]["kernel_includes_transfer"] is True


@needs_gcc
@needs_make
@pytest.mark.parametrize("args,msg", [
    (["--reps"], "missing its value"),
    (["--reps", "12abc"], "needs an integer"),
    (["--samples", "0"], "needs an integer"),
    (["--threads", "-1"], "needs an integer"),
    (["--n-examples", "10", "--chunks", "3"], "divisible"),
    (["--n-examples", "9223372036854775807", "--samples", "2", "--chunks", "1"], "too large"),
    (["--n-examples", "99999999999999999999", "--chunks", "1"], "needs an integer"),
    (["--bogus", "1"], "unknown option"),
])
def test_offload_rejects_bad_arguments(emit_test, args, msg):
    p = run([str(emit_test), *args])
    assert p.returncode == 2 and msg in p.stderr


@needs_gcc
def test_toolchain_selftest(tmp_path):
    p = run(["sh", str(ROOT / "mic/toolchain/selftest.sh"), str(tmp_path)])
    assert p.returncode == 0, p.stdout + p.stderr


def test_elf_target_on_host_binary():
    p = run(["sh", str(ROOT / "mic/toolchain/elf-target.sh"), str(find_binary("knc_bench"))])
    assert p.returncode == 1 and p.stdout.strip() == "X86_64"


@needs_gcc
@needs_make
def test_gcc_k1om_profile_refuses_non_k1om_output(tmp_path):
    fake = tmp_path / "bin" / "x86_64-k1om-linux-gcc"
    fake.parent.mkdir()
    fake.write_text('#!/bin/sh\nexec gcc "$@"\n')
    fake.chmod(0o755)
    out = tmp_path / "build"
    p = run(["make", "-C", str(ROOT / "mic/native"), "TOOLCHAIN=gcc-k1om", f"CC={fake}", f"OUT={out}"])
    assert p.returncode != 0
    assert "not a K1OM executable; removed" in p.stderr
    assert not any(out.glob("knc_*"))  # nothing left behind


def test_deploy_refuses_host_binaries(tmp_path):
    shutil.copy(find_binary("knc_bench"), tmp_path / "knc_bench")
    shutil.copy(find_binary("knc_validate"), tmp_path / "knc_validate")
    shutil.copy(find_binary("knc_infer"), tmp_path / "knc_infer")
    env = {**os.environ, "OUT": str(tmp_path), "MIC_LIBDIR": str(tmp_path)}
    p = run(["sh", str(ROOT / "mic/native/deploy.sh"), str(ROOT / "configs/tiny.yaml")], env=env)
    assert p.returncode == 1 and "is not a K1OM executable" in p.stderr


def test_run_sweep_failures_are_reported(tmp_path, tiny_dataset):
    good = run([str(find_binary("knc_bench")), "--kernel", "magsq", "--synthetic", "--samples", "64",
                "--batch", "1", "--reps", "2"]).stdout
    (tmp_path / "good.json").write_text(good)
    stub = tmp_path / "bin" / "ssh"
    stub.parent.mkdir()
    stub.write_text('#!/bin/sh\ncase "$*" in *"--threads 2 "*) echo \'boom "quoted" \\\\ x\' >&2; exit 7;; '
                    f'*) cat {tmp_path / "good.json"};; esac\n')
    stub.chmod(0o755)
    env = {**os.environ, "PATH": f"{stub.parent}:{os.environ['PATH']}", "KERNELS": "magsq", "BATCHES": "1",
           "THREADS": "1 2", "LOGDIR": str(tmp_path / "logs")}
    p = run(["sh", str(ROOT / "mic/native/run_sweep.sh"), "test.kncds"], env=env, cwd=tmp_path)
    assert p.returncode == 1
    assert "4 configurations, 2 ok, 2 failed" in p.stderr
    recs = [json.loads(line) for line in p.stdout.splitlines()]
    assert sum(r["status"] == "ok" for r in recs) == 2
    bad = [r for r in recs if r["status"] == "unavailable"]
    assert len(bad) == 2 and "exit 7" in bad[0]["reason"] and "boom" in bad[0]["reason"]
    for r in bad:
        validate(r, sub="missing_record")


def test_knc_bench_records_toolchain_and_team_size():
    p = run([str(find_binary("knc_bench")), "--kernel", "magsq", "--synthetic", "--samples", "64",
             "--batch", "2", "--threads", "2", "--reps", "2"])
    r = json.loads(p.stdout)
    validate(r, sub="ok_record")
    assert r["metadata"]["openmp"]["team_size"] == 2
    assert r["metadata"]["toolchain"]["name"] == "cmake-host"


def test_knc_bench_rejects_huge_batch():
    p = run([str(find_binary("knc_bench")), "--kernel", "magsq", "--synthetic", "--samples", "1024",
             "--batch", "9000000000000000"])
    assert p.returncode == 2 and "too large" in p.stderr


def test_host_check_runs_and_reports(tmp_path):
    """In this VM: must flag the hypervisor, never crash, exit 1 on FAILs."""
    p = run(["sh", str(ROOT / "mic/host/host-check.sh"), str(tmp_path / "hc")])
    summary = (tmp_path / "hc" / "SUMMARY.txt").read_text()
    for check in ("bare-metal", "vt-x", "iommu-active", "kvm", "card", "governor", "cpu-power", "memory"):
        assert f" {check} " in summary, check
    assert p.returncode == (1 if "FAIL " in summary else 0)


def test_host_check_card_branch_with_stub_lspci(tmp_path):
    """A card that lspci reports but sysfs does not know must give verdicts, not a crash."""
    stub = tmp_path / "bin" / "lspci"
    stub.parent.mkdir()
    stub.write_text('#!/bin/sh\ncase "$*" in\n'
                    '  *"-D -d 8086:2250"*) echo "0000:3b:00.0 Co-processor: Intel Xeon Phi 5110P";;\n'
                    '  *-vvv*) printf "\\tRegion 0: Memory at 38000000000 (64-bit, prefetchable) [size=8G]\\n'
                    '\\t\\tLnkCap:\\tPort #0, Speed 5GT/s, Width x16\\n\\t\\tLnkSta:\\tSpeed 5GT/s, Width x8\\n";;\n'
                    '  *) echo "3b:00.0 Co-processor: Intel Xeon Phi 5110P";;\nesac\n')
    stub.chmod(0o755)
    env = {**os.environ, "PATH": f"{stub.parent}:{os.environ['PATH']}"}
    p = run(["sh", str(ROOT / "mic/host/host-check.sh"), str(tmp_path / "hc")], env=env)
    s = (tmp_path / "hc" / "SUMMARY.txt").read_text()
    assert "PASS  card-bar0" in s and "[size=8G]" in s
    assert "WARN  card-link" in s and "Width x8" in s      # x8 link is flagged
    assert "FAIL  card-iommu" in s                          # no sysfs group here
    assert p.returncode == 1
