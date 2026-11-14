# Typed RPC: matched controls and raw evidence

Public entry points are documented in [Typed RPC](../../docs/typed_rpc.md).
Root tests include the supported module; the standalone build below remains
available for focused validation. The historical results under `results/` retain
the original prototype paths, hashes and validation limitations.

Base Portal: `19aa5491039f47db54a3b56d2ba26ee33a5a460f`.
Intended FF integration target: `f6b32f7bc2443e9f2a9658430340a4119637b804`.
The FF checkout was unavailable locally: **no FF timing/validation result** is
claimed. Core tests use real, blob-verified Portal async/optional/inplace code.

See [API/wire/ownership guide](../../docs/typed_rpc.md) and
[recorded report](results/2026-11-14-sandbox/report.md).

## Build from a complete checkout

```sh
cmake -S tests/typed_rpc -B build-typed -DCMAKE_BUILD_TYPE=Release \
  -DPORTAL_TYPED_RPC_BUILD_BENCHMARKS=ON
cmake --build build-typed --parallel 2
ctest --test-dir build-typed --output-on-failure
python3 benchmarks/typed_rpc/run.py \
  --binary build-typed/typed_probe \
  --allocations build-typed/typed_allocations \
  --out /tmp/portal-typed-results --repetitions 7
```

Windows multi-configuration builds use `--config Release` and binaries in the
`Release` subdirectory. MSVC/Windows has not been tested in this experiment.
The allocation-counter target needs exceptions; it is separate from the timing
binary. The source also supplies a benchmark-free strict/no-exception test gate.
No compiler, test process or second benchmark is intentionally run concurrently
with a timing phase. The runner pins child processes to an available CPU on
platforms supporting `sched_setaffinity`; pinning does not isolate a shared host.

## Explicit isolated legacy control used in the recorded sandbox

The complete repositories could not be downloaded. The recorded build enabled:

```sh
-DPORTAL_TYPED_RPC_ISOLATED_LEGACY=ON
```

That option invokes the already-existing `benchmarks/reuse_pass/extract_wire.py`
on the blob-verified current `rpc.h`. It preserves the actual codec/router and
workspace bodies, replacing unrelated backend template declarations with the
minimal trait definitions needed for the supported **legacy Binder ABI**. The
existing `rpc_reuse.h` is used directly. There is no FF stub and no invented
replacement RPC implementation. The typed headers and actual Portal
async headers compile normally, without codec extraction.

Consequently the benchmark measures codec/router + identical legacy binder body,
not generated `.def` dispatch/overload/converter performance. A full checkout
build defaults to the real original backend headers, but still intentionally
uses the same legacy Binder ABI for this comparison. The isolated recorded
measurements do not establish full repository integration.

## Lanes and their limits

| Lane | Purpose |
|---|---|
| `dynamic_fresh` | Current owning RPC request/vector/reply allocation path |
| `dynamic_reuse` | Current accepted argument-vector + byte-buffer reuse path; primary baseline |
| `legacy_static` | Same original wire bytes, known-signature server decode without dynamic argument values |
| `typed_reuse` | New checked static codecs + general typed router + explicit byte cache |
| `typed_fresh` | Same new path with zero cached capacity |
| `hand_reference` | Per-method specialized same-new-format checked reference, no general routing table |

`legacy_static` is monomorphic, not a second complete generic router. It also
uses the new checked scalar reader/writer inside the server. Thus it is evidence
about avoiding general representation/dispatch work, not a clean isolation of
one single instruction or the value container alone. `hand_reference` shares new
low-level codecs/envelope checks; it is **not an independent handwritten codec**,
a universal lower bound, or proof of minimal possible latency.

All lanes call the same noinline native arithmetic/owning echo function and
verify the result on every iteration. A noinline transport wrapper prevents the
entire loopback byte pipeline from simply collapsing into the native operation.
The lane selection occurs outside the measured loop. These choices do not
eliminate every compiler/code-layout interaction. Successful-call semantics are
matched; wire formats, error reporting and overload contracts differ, so the
new path is not advertised as a transparent old-protocol replacement.

## Measurement and retention

Each independent process warms up 4,096 calls. Seven rounds rotate/shuffle lanes
and payload cases with seed 20261114. Scalar uses two **i64**, not narrowed i32.
Measured counts: scalar 2,000,000; 16-byte 500,000; 1-KiB 200,000; 64-KiB 5,000.
Strings contain owning binary data; every echo is compared against the input.
Payload copies are not removed by changing results into borrowed views.

All cache-enabled lanes explicitly use **1 MiB per byte buffer**. Defaults remain
64 KiB; a 64-KiB payload plus headers exceeds the default. Allocation counts are
collected with a separately compiled interposition binary after warmup; timing
has no global-new interposition. Counts cover C++ allocation requests, not libc
internal allocations, system calls or RSS. Ordinary scalar reuse is already
zero-allocation, an important control.

`run.py` preserves every sample, command, stdout/stderr, checksum, environment
and binary hash. Summary rows are medians of per-process average nanoseconds,
not per-request percentiles or confidence intervals. No outliers are discarded.
No network transport, fixed-rate open-loop arrival test, p99, deployment load or
FF scheduling benchmark was run here.

## Retained negative results and factor experiment

The first candidate used string resize+copy and cleared cached byte vectors.
Its 64-KiB reuse result regressed. `experiments/*.patch` transform the selected
headers **back to** each factor control; they are experiments, not additional
recommended patches. Apply them only in disposable checkout copies.

All four binaries were compiled before measurement, with GCC14.2/C++14/O3,
`-DNDEBUG -pthread`, and the same `probe.cpp` and legacy extraction. Five serial
rotating rounds then compared each factor's `dynamic_reuse` and `typed_reuse`
at 64 KiB / 10,000 operations. Raw commands, hashes and stdout are retained under
`results/2026-11-14-sandbox/factors/`. The final `selected/` acceptance study was
rebuilt/repeated after source selection; it does not reuse factor medians.

The independent allocator-environment diagnostic can be reproduced with:

```sh
python3 benchmarks/typed_rpc/allocator_control.py \
  --binary build-typed/typed_probe --out /tmp/typed-allocator-control
```

It compares default child environments and `MALLOC_TRIM_THRESHOLD_=-1` at 64 KiB,
with five rotated rounds. This is a diagnosis, not a deployment recommendation.
The library and normal runner do not alter allocator tunables.
