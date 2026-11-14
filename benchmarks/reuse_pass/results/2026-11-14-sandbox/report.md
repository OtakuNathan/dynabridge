# Portal reuse pass: results, decisions and evidence limits

Baseline: `365b17d76df178d660acee557292c78e44d217c0`.
This pass keeps **explicit owning RPC storage reuse**, retains borrowed decoding
as an isolated experiment, and rejects the tested async pool. It does not alter
ordinary imported delegates or replace any completion-token lifetime guarantee.

## What is actually validated

The local environment had five hash-verified upstream production headers, not
a complete Portal checkout. RPC measurements/tests used the **actual value,
codec and router bodies** extracted from `rpc.h`, with minimal declarations for
the supported legacy Binder ABI. The generated-backend tail was excluded. This
is neither a mock codec nor full generated-binder integration. The async tests
compiled actual complete async/optional/inplace headers without that shim.

The final portable driver reproduced the `.text` hashes of all five original
timing binaries; see `validation/driver-codegen-match.json`. The full-checkout
CMake mode includes an additional generated-binder/overload/conversion test,
but that gate was **not executed here**. The whole-project CMake/CI, FF, Python,
Node, MSVC and ARM suites have not been rerun for this patch. Adoption should be
conditional on the provided full-checkout gate and the application's workloads.

## Production changes kept

Only `portal/backends/rpc.h` and the new `portal/backends/rpc_reuse.h` change
production behavior, and only through explicit new entry points:

- `router::dispatch_into(frame, output, dispatch_workspace)` retains empty
  argument-vector capacity and reuses caller-owned reply storage.
- `rpc::round_trip_reusing(transport, call_workspace, method, values...)` reuses
  request/response storage around a synchronous `round_trip_into` transport.
- `reusing_loopback_transport` supplies that explicit transport contract.

There is no implicit global/TLS cache, no changed protocol, no default allocator
switch and no borrowed string delivered to a custom binder. The old writer,
encoders, checked decoder, router dispatch and typed backend invoke bodies are
unchanged. Production `portal/async.h` remains the upstream blob
`8097f5be327d77c0d191f4cfa242bc19703232bf`.

Each active call owns its vector/buffers; nested synchronous calls take different
storage. Completed scopes keep at most one bounded empty allocation per cache.
Default idle retention budgets are 64 value slots and 64 KiB per request/response
buffer. Limits apply to **capacity**, not input size, total live memory, recursion
depth or an allocator's hidden caches. Cached bytes are not securely zeroed.

## Method

Linux x86-64 sandbox; `/proc/cpuinfo` reports AMD EPYC 9V74. GCC 14.2.0,
libstdc++, glibc 2.41; C++14, `-O3 -DNDEBUG -pthread`, no LTO. Timing was pinned to
allowed CPU 0, with no isolated-core/host-frequency guarantee. The process
allowed CPUs were 0–4. A thread was created and joined before each timed probe
to avoid relying on a permanently single-threaded runtime fast path.

The primary matrix used **168 independent processes**, seven repetitions per
variant, rotating variant order; processes ran serially after compilation.
There was a 10,000-operation warmup in every process. Tables report the **median
of per-process mean costs**; brackets are full observed process min–max, not
confidence intervals or per-operation percentiles. All observations, including
slow ones, are retained. Faster-pair counts compare the same repetition number;
percentage changes compare the medians, not median paired percentages.

Separate instrumented binaries counted global C++ `operator new` calls after
warmup. Instrumented clock values were not used for performance conclusions.
The zero count field printed by a non-instrumented timing binary means
**unmeasured**. Counts are not a claim about every libc/runtime allocation or
about arbitrary application converters.

The RPC probe serializes changing scalar inputs or an unchanged echo payload,
decodes it, invokes an identical supported legacy binder, encodes a response and
decodes an owning result. It verifies a checksum. This excludes generated schema
selection/conversion, networking, queueing and FF scheduling. It has one live
request per timing thread. Four-thread tests below establish correctness only.

## 1. Explicit buffer reuse

The probe uses a **1 MiB retention budget per byte buffer** so that a 64-KiB
payload plus wire headers fits. That is an explicit benchmark configuration, not
the new default. A 64-KiB payload does not fit the default 64-KiB frame budget.

| RPC case | Original owning ns/op, median [range] | Reuse ns/op, median [range] | Median cost change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| Two signed scalar arguments | 111.053 [107.107–147.030] | 101.131 [96.237–109.725] | −8.9% | 7/7 |
| Echo 16 B | 141.302 [135.922–234.280] | 115.497 [111.976–122.678] | −18.3% | 7/7 |
| Echo 1 KiB | 237.891 [215.404–268.787] | 201.519 [183.488–209.278] | −15.3% | 7/7 |
| Echo 64 KiB, default glibc policy | 23,725.324 [23,210.792–26,208.768] | 6,410.184 [5,978.478–6,701.302] | −73.0% | 7/7 |

These results justify offering an explicit path for suitably hot, bounded-lifetime
workloads. They do not justify changing the default ownership/storage policy.

### The 64-KiB result is highly allocator-policy-dependent

A follow-up control changed only the test process's environment to
`MALLOC_TRIM_THRESHOLD_=-1`, used the same binaries and CPU, 20,000 timed calls,
and five alternating pairs:

| Policy control | Original ns/op, median [range] | Reuse ns/op, median [range] |
| --- | ---: | ---: |
| `MALLOC_TRIM_THRESHOLD_=-1`, echo 64 KiB | 5,531.110 [5,495.062–6,301.021] | 5,491.834 [5,432.059–5,624.284] |

The median gap becomes only **0.7%**, with reuse faster in **3/5 pairs** and
substantial range overlap. Thus the 73% default-policy gap is **not a general
copy-bandwidth or intrinsic serializer improvement**. The payload-copy work is
not removed. The data indicate a strong interaction with allocation/reclamation
policy; no syscall trace was available to identify its precise internal cause.
The control and original series are both retained. Disabling automatic reclamation
is not recommended as an application change here; it trades memory behavior
and is only a diagnostic control.

GNU's documented trim threshold controls when releasable top-of-heap storage
triggers return-to-system work, and can be set using this environment variable:
[GNU C Library: Malloc Tunable Parameters](https://sourceware.org/glibc/manual/latest/html_node/Malloc-Tunable-Parameters.html).

### Allocation evidence

Separate 10,000-operation runs, warmed storage, same libstdc++/libc host:

| Case | Original C++ new calls/op | Reuse calls/op |
| --- | ---: | ---: |
| Scalar | 3 | 0 |
| 16 B, 1 KiB or 64 KiB echo | 5 | 2 |

The eliminated operations are the request frame, argument-vector backing storage
and response frame. String payload ownership remains; request and final-response
strings still allocate in these cases on this host. A different string SSO layout,
converter, concurrent live set or allocator can change these counts/costs.

### Old-entry control and code layout

The primary candidate binary deliberately contains **both** ordinary and reuse
paths. Its ordinary-path medians were 117.497, 145.757, 254.011 and 23,980.748 ns
for the four cases respectively; these are worse than their original-binary
medians. They are retained in `summary.csv`, not discarded.

A matched legacy-only build, excluding the unused reuse benchmark instantiation,
produced **byte-identical `.text`** from old and new headers under the same flags.
Its SHA-256 is `47423cae5e11067d4c83adfb79359b576895893467453b10aa302dbed1e34a3d`.
This supports the narrower claim that the original path was left intact in this
probe; it does not promise identical application layout or performance on every
compiler. The favorable table uses the original baseline, not the slower
candidate-binary ordinary medians as a convenient denominator.

## 2. Borrowed decode: useful experiment, not a generic replacement

An independent checked view decoder was differentially exercised against the
owning parser. Both timing sides reuse their argument-container capacity.
`borrow-owning` constructs an owning string from the view before consuming it,
so it retains the same string allocation/copy obligation. These are **decoder-
only** timings, not full router or generated-binder RPC measurements.

| Payload | Owning ns/op, median [range] | View then owning ns/op, median [range] | Change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| 16 B | 28.271 [27.106–34.088] | 25.152 [23.564–26.918] | −11.0% | 7/7 |
| 1 KiB | 49.881 [44.313–57.334] | 35.933 [31.637–44.436] | −28.0% | 7/7 |
| 64 KiB | 1,480.090 [1,404.581–1,577.204] | 1,410.785 [1,320.083–1,441.502] | −4.7% | 5/7 |

Both owning modes still have **one string new call per operation** in the
instrumented probe. The smaller view representation and different parsing path
can help, but integrating it through overload probing, custom converters and
possible retention would require a separate lifetime contract and more testing.
It is deliberately not routed into ordinary binders in the production patch.

The `borrow-readonly` median was around 10–12 ns across these payloads. It only
reads length and **one byte**, does not construct an owning string and does not
scan the whole payload. Treating that as a 64-KiB RPC speedup would be invalid.
Views must never outlive or observe mutation of their input frame. The prototype
is stored under `experiments/`, not the installed backend headers.

## 3. Async allocation policy: the tested pool loses

An experimental allocator-tag `start` overload uses `allocate_shared` while
preserving copied-token ownership, local self-destruction pins, locks, detach,
retirement and cleanup. A bounded synchronized exact-size pool owns its resource
through the allocator retained by the shared control block. Counters were
compiled **out** of timing binaries; they were enabled only in contract/counting
runs. Its synchronization/ownership is real cost, not measurement instrumentation.

| Inline success path | ns/op, median [range] |
| --- | ---: |
| Original `make_shared` implementation | 52.892 [49.348–60.433] |
| Experimental factory, system allocation | 46.034 [44.114–49.071] |
| Same factory, synchronized pool | 99.046 [97.067–103.408] |

Against the same-factory system control the pool costs **115.2% more**, and loses
all seven pairs, despite reducing warmed backing `operator new` calls from
1/op to 0/op. This is a one-live-state, same-thread inline workload in an already
multithreaded process, not a general verdict on every allocator or a contended
application workload. The factory-only system median is also recorded; we did
not retain a new production policy API solely for an unexplained code-generation
change in this microprobe.

The experiment passed resource-lifetime, self-destroying callback, retained-token,
late completion after detach, cross-thread final release, rejection, allocation-
failure and concurrent pool tests. Correctness did not make it faster. The pool
and allocator hook remain reproducible experimental files; **production async.h
is restored exactly**, including the prior UAF fix.

## Correctness and validation record

Final isolated CMake suites passed **7/7 with GCC** and **7/7 with Clang**, C++14,
Release, warnings as errors. These include ordinary/strict-no-exception RPC
contracts, allocation fault injection, borrowed differential parsing, the existing
actual async cleanup regression and the optional allocator experiment. This is
not the complete repository's earlier 44-test suite.

Focused final GCC runs passed ASan+UBSan **with leak detection enabled**, and TSan
for the changed RPC core. These exercise all wire kinds, NaN/negative-zero bit
transport, binary/empty/large strings, encode parity, truncated/mutated frames,
unknown methods, reject-before-call, owning outputs and retained legacy values,
request/reply aliasing (including strings), same-workspace recursive dispatch,
cache limits and release, and four concurrent callers with distinct workspaces.
Allocation fault injection hit five different cold call allocations, followed by
successful reuse; throwing and malformed transports were also covered.

The borrowed parser passed 2,000 valid randomized frames and 16,000 mutated
comparisons plus ASan/UBSan/LSan. The experimental allocator passed ASan/UBSan/LSan
and a separate TSan run at `-O0`. Its initial `-O1 -Werror` TSan build was rejected
by GCC's maybe-uninitialized analysis on optional lambda storage; that log is
retained. No production warning suppression or optional-storage change was made.
The final original async cleanup regression passed ASan/UBSan/LSan too.

An added legacy-retention test initially triggered GCC's unused-local-typedef
warning; moving that test fixture to namespace scope fixed it without altering
production code. The initial diagnostic and successful final logs are retained.

The portable measurement driver itself was compiled and smoke-run for all modes.
Its five timing executable `.text` hashes match the originally measured ones.
No claim is made about whole-project CI, generated-binder integration, physical
I/O, p99/p99.9, production-scale producer counts, or long-lived memory pressure.

## Files and reproduction

- `timings.csv`, `summary.csv`, `comparisons.json`: complete primary observations.
- `processes.json`: exact primary process commands/stdout/stderr (168 processes).
- `allocation-observations.csv`: separate allocation counts (19 processes).
- `allocator-trim-control*.{csv,json}`: five paired allocator-policy controls.
- `provenance.json`: source/binary hashes, build commands, compiler and host.
- `validation/`: test logs, initial compiler diagnostics and code-generation checks.
- `original_measurements.py`: frozen original sandbox runner, with absolute paths.

Use the portable [driver](../../run.py) and [README](../../README.md) for a new
checkout. Build the normal full-checkout test mode before accepting this opt-in
path into an application. The [public usage/contract guide](../../../../docs/rpc_reuse.md)
explains worker confinement, retained-capacity budgets and synchronous transport
lifetimes. The separate prior MSVC `rpc_storage_test.cpp` fix is not duplicated
in this patch; the files changed by the two patches do not overlap.
