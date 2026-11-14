# Portal reuse experiments

Baseline: `365b17d76df178d660acee557292c78e44d217c0`.
Production candidate: explicit owning RPC buffer/argument-storage reuse only.
The borrowed parser and asynchronous allocation policy are **experiments**, not
new default behavior. Production `portal/async.h` is byte-for-byte unchanged.

Read [usage and contracts](../../docs/rpc_reuse.md) and the
[recorded report](results/2026-11-14-sandbox/report.md) before interpreting numbers.

## Full-checkout tests

```sh
cmake -S tests/reuse_pass -B build-reuse -DCMAKE_BUILD_TYPE=Release
cmake --build build-reuse --parallel 2
ctest --test-dir build-reuse --output-on-failure
```

The full mode includes a generated-binder/overload/conversion gate. Normal and
GCC/Clang strict=0/1 no-exception reuse tests run without enabling any allocator
experiment. The root project's CI list is not modified by this patch.

To test the rejected allocator policy too, configure with
`-DPORTAL_REUSE_ALLOCATOR_EXPERIMENT=ON`. Python and git copy the headers to the
build directory and apply `experiments/async_allocator.patch` **there**. The
source checkout's `async.h` stays unchanged. The pool is a bounded synchronized
benchmark fixture, not a recommended general-purpose allocator.

## Timing and allocation experiments

With the candidate applied, create a separate local baseline worktree:

```sh
git worktree add --detach ../portal-before-reuse 365b17d76df178d660acee557292c78e44d217c0
python3 benchmarks/reuse_pass/run.py \
  --baseline ../portal-before-reuse --out /tmp/portal-reuse-results \
  --cxx g++ --cpu 0 --repeats 7
```

Use an allowed CPU, or omit `--cpu` where taskset is unavailable. The script
needs a GCC/Clang-compatible driver, Python 3.9+, git, and local sources; it does
not download dependencies or write to either checkout. Compilation finishes
before timing. Each variant runs as a separate process, serially, with rotating
order. `--build-only` and `--run-only` separate these phases further; pass the same
source/output and isolation settings. `--scale` changes iterations, not the
10,000-operation warmup, and must be reported with any comparison.

Outputs include build commands, hashes, host/compiler information, raw stdout,
process-level means, medians/ranges, and **separate** operator-new counts. Timing
binaries contain no allocation counter; their trailing `0` count field means
**unmeasured**, not zero. The RPC fixture uses Portal's supported legacy Binder
ABI on both sides; it does not measure generated-overload selection or Python,
Node, a network transport, or any service tail-latency guarantee.

The byte-workspace benchmark budget is explicitly 1 MiB per cached buffer.
Default production budgets remain 64 KiB per buffer and 64 argument slots.

### Allocator-policy sensitivity control

The 64-KiB default-glibc result is not portable across allocator settings. After
building, reproduce the recorded diagnostic separately from ordinary timing:

```sh
MALLOC_TRIM_THRESHOLD_=-1 taskset -c 0 /tmp/portal-reuse-results/bin/rpc-before ordinary 65536 20000
MALLOC_TRIM_THRESHOLD_=-1 taskset -c 0 /tmp/portal-reuse-results/bin/rpc-after reuse 65536 20000
```

Rotate order and repeat. These environment assignments apply only to those test
processes. They are **not** a recommendation to disable reclamation in an
application. The large observed default-policy gap nearly disappeared in this
control. Both series are retained; no syscall trace was available to establish
the precise allocator mechanism.

## Isolated diagnostic mode used for the supplied results

Only five upstream production headers were available as hash-verified local
files in the recorded environment; complete native binder/backend dependencies
were unavailable. `--isolated` explicitly extracts the actual value, codec and
router bodies from `rpc.h`, supplies minimal trait declarations for the legacy
Binder ABI and excludes the unrelated `rpc_backend` tail. No timed codec/router
body is replaced, but this is **reduced integration coverage**.

The CMake analogue is `-DPORTAL_REUSE_ISOLATED=ON`. In this mode the full generated-
binder test is omitted rather than passed or counted as a skip. Ordinary async
and allocator experiments still compile the actual complete async/optional
headers, without a wire shim. The committed result provenance records isolation.
The default commands above intentionally do not request this fallback.

## Decisions

- Keep explicit buffer/argument-vector reuse; old APIs are unchanged.
- Retain the borrowed decoder as a separate representation experiment. Borrowing
  followed by owning conversion still allocates/copies a string. The read-only
  experiment checks length and one byte, **not** every payload byte or a full RPC.
- Reject this shared-resource synchronized async pool: it removed warmed backing
  allocations but more than doubled the same-policy inline mean cost. Local token
  lifetime pins, locks, detach/retire semantics are not removed for a benchmark.

`results/.../original_measurements.py` is the frozen sandbox command record and
contains absolute paths. Use `run.py` for portable reproduction.
