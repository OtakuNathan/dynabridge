# Portal typed RPC experiment — 2026-11-14

## Decision and delivery scope

Keep this as an **opt-in experiment**, not a silent replacement for either
legacy RPC or FFI. The measured static wire/router path has a repeatable advantage
against the existing explicit storage-reuse baseline in this in-memory probe.
The new async adapter uses the actual Portal static invocation lifetime machinery.
No existing production header is modified. No GitHub write/commit was made.

This delivers three additive headers:

- `wire_schema.h`: C++14 field/method codec building blocks and record X-macro.
- `typed_rpc.h`: versioned checked envelope, direct typed routing and explicit
  worker-confined buffer reuse.
- `typed_rpc_async.h`: owning transport request/response adapter to existing
  concrete Portal `static_invocation`.

The existing generated Portal `call_*` entry points and backend lowering path
are **not** redirected. The method table projects a native `signature_t`, but
transparent integration with the existing callable generator remains unvalidated
and is intentionally not installed in shared core. FF uses the existing concrete
invocation adapter; a real-FF reusable-blueprint gate is supplied but was not run.
This is not a complete network stack or a claim of a finished universal RPC API.

Base: Portal `19aa5491039f47db54a3b56d2ba26ee33a5a460f`.
Target FF: `f6b32f7bc2443e9f2a9658430340a4119637b804` (not locally available).

## Final selected acceptance result

Environment: Linux x86-64 sandbox, reported AMD EPYC 9V74, glibc 2.41,
GCC 14.2.0, C++14 `-O3 -DNDEBUG`, no LTO. Process pinned to allowed CPU 0;
no exclusive-core/frequency/thermal isolation. The timing binary does **not**
interpose global new. Each lane is run in a separate process; seven rotating
randomized matched rounds, seed 20261114. Every result/checksum is validated.
No timing outliers were discarded.

Times are medians of **per-process average ns/call**, not individual-request
p50/p99 and not network/FF performance. The primary baseline is the latest
accepted RPC storage-reuse implementation, not its older unoptimized ancestor.

| Workload | Current reuse ns | Typed reuse ns | Change | Faster pairs | Allocation calls: current / typed |
|---|---:|---:|---:|---:|---:|
| Two i64 arguments / i64 result | 118.305 | 39.569 | -66.6% | 7/7 | 0 / 0 |
| 16-byte owning echo | 142.181 | 92.549 | -34.9% | 7/7 | 2 / 2 |
| 1-KiB owning echo | 215.286 | 164.220 | -23.7% | 7/7 | 2 / 2 |
| 64-KiB owning echo | 7,126.529 | 6,398.102 | -10.2% | 7/7 | 2 / 2 |

Both scalar reuse paths perform zero measured hot C++ allocations. String reuse
paths both perform two owning payload allocations. Therefore the improvement is
not explained by merely removing allocation calls or borrowing the result. It
combines avoiding dynamic value materialization/probing, different codec/write
structure and buffer initialization decisions; this experiment does not isolate
a single instruction or attribute the entire gap to X-macros/type tags.

All cache-enabled lanes explicitly use a 1-MiB budget per byte buffer. Library
defaults remain 64 KiB; 64-KiB payloads plus headers exceed that default. Cache
budgets bound idle capacity, not total active requests or memory. Payload copies
remain owning and are not claimed eliminated.

### All lanes, including unfavorable results

| Payload bytes | Lane | Median ns | Process min–max ns | Allocations/call |
|---:|---|---:|---:|---:|
| 0 | dynamic_fresh | 135.343 | 125.898–173.073 | 3 |
| 0 | dynamic_reuse | 118.305 | 108.507–149.213 | 0 |
| 0 | legacy_static | 92.508 | 78.433–105.568 | 0 |
| 0 | typed_reuse | 39.569 | 34.951–48.804 | 0 |
| 0 | typed_fresh | 64.654 | 58.629–68.763 | 2 |
| 0 | hand_reference | 31.422 | 27.667–35.063 | 0 |
| 16 | dynamic_fresh | 172.896 | 146.427–240.191 | 5 |
| 16 | dynamic_reuse | 142.181 | 125.762–170.625 | 2 |
| 16 | legacy_static | 109.207 | 101.185–138.558 | 2 |
| 16 | typed_reuse | 92.549 | 78.420–128.987 | 2 |
| 16 | typed_fresh | 127.466 | 110.594–170.407 | 4 |
| 16 | hand_reference | 88.619 | 78.509–117.656 | 2 |
| 1024 | dynamic_fresh | 265.538 | 237.460–296.022 | 5 |
| 1024 | dynamic_reuse | 215.286 | 199.873–281.575 | 2 |
| 1024 | legacy_static | 195.208 | 176.465–239.402 | 2 |
| 1024 | typed_reuse | 164.220 | 140.678–179.378 | 2 |
| 1024 | typed_fresh | 228.150 | 207.997–306.408 | 4 |
| 1024 | hand_reference | 152.476 | 135.779–294.356 | 2 |
| 65536 | dynamic_fresh | 7,386.611 | 7,005.277–13,396.006 | 5 |
| 65536 | dynamic_reuse | 7,126.529 | 6,839.245–12,501.464 | 2 |
| 65536 | legacy_static | 7,172.183 | 6,761.304–8,016.030 | 2 |
| 65536 | typed_reuse | 6,398.102 | 5,836.294–7,131.585 | 2 |
| 65536 | typed_fresh | 24,004.772 | 21,805.211–28,790.054 | 4 |
| 65536 | hand_reference | 6,222.643 | 6,043.172–8,169.237 | 2 |

`dynamic_fresh` is the current allocating baseline. `typed_fresh` uses the new
protocol with no retained buffers; at 1 KiB it is slightly slower than the current
reusing baseline, and at 64 KiB it is dramatically slower. This is why the retained
positive result is explicitly the **reusing** path, not a blanket new-protocol win.

`legacy_static` preserves the original wire bytes while directly decoding known
server argument types. The two-i64 result is about 22% below current reuse, but
this control is monomorphic and bypasses general route handling as well as dynamic
parameter storage. It also uses the new bounded writer/reader on the server.
It is suggestive evidence, not a clean attribution of that entire 22% to one layer.

`hand_reference` is method-specialized, with the same new envelope, checks,
owning values and native function. It skips general routing. It shares lower-level
codec helpers and is not an independent handwritten implementation or a universal
lower bound. The final typed scalar path remains slower than this reference;
no zero-overhead or fully optimized claim is made.

All original baseline timing uses the real current codec/router bodies and the
same supported **legacy Binder ABI**. It does not include generated `.def`
overload/converter overhead. No network, queue scheduling, FF integration timing,
open-loop arrivals or tail-latency workload was measured.

## Negative result that changed the implementation

The first candidate initialized owning strings with `resize` before copying,
and cleared cached byte vectors before each frame. Its seven-round 64-KiB
`typed_reuse` median was 8,596.542 ns versus 6,836.013 ns for current reuse:
about **25.8% slower**, zero of seven faster pairs. Those observations remain in
`initial/`; they were not removed from the evidence.

An additional five-round 2x2 factor experiment tested direct string assignment
and retaining initialized byte-vector elements separately:

| Factor | Current reuse median ns | Typed reuse median ns |
|---|---:|---:|
| resize_both | 6,826.059 | 8,695.329 |
| assign_string | 6,815.229 | 7,316.495 |
| retain_bytes | 7,078.058 | 7,673.121 |
| selected | 6,814.055 | 6,028.668 |

These are separate process medians, not numbers to combine arithmetically.
All variants used the same probe, native function, payload and owning semantics.
Combining the changes removed the regression in this experiment. The selected
implementation then received the independent seven-round acceptance run above.
Reverse factor patches and exact header/binary hashes are retained. The timing
supports these choices but no instruction/cache/system-call trace was collected
to prove a unique microarchitectural explanation.

Retaining byte size is safe only because the encoder sets the exact published
size and overwrites every byte. No uninitialized capacity is written via
`vector::data()` beyond size. No borrowed payload escapes. Idle caches may retain
old data until reused/released; this is explicitly not secure erasure.

## Allocator-environment diagnostic

The fresh 64-KiB regression prompted a separate five-round diagnostic using the
**same final binary**, 10,000 operations/process, randomized lanes and settings.
The process environment only changes in the spawned diagnostic children:

| Environment | Dynamic fresh ns | Typed fresh ns | Dynamic reuse ns | Typed reuse ns |
|---|---:|---:|---:|---:|
| default | 7,177.739 | 22,280.178 | 6,708.591 | 5,981.610 |
| trim_disabled | 6,857.599 | 7,113.137 | 7,022.834 | 6,299.056 |

The large fresh-path penalty becomes much smaller with
`MALLOC_TRIM_THRESHOLD_=-1`; reusable buffers retain an advantage in this series.
This establishes strong sensitivity to that allocator setting. It does not prove
the exact number of trim/syscall events, nor that this is the only cost. No such
setting is changed by the library, and disabling memory reclamation is not a
recommended performance fix. `allocator-control/` retains all observations.

## Wire and contract costs, not hidden benefits

The request/reply envelope is 32 bytes, including magic/version, status, explicit
method ID/revision, payload length and a deterministic 64-bit signature fingerprint.
Two i64 arguments use 16 payload bytes, but the full request is 48 bytes versus
35 in the old format. An i64 response is 40 versus 15 bytes. Even the two-i32 case
has a 40-byte total request versus 35 legacy bytes. **This prototype is not a
universal packet-size reduction.** The scalar benchmark uses i64 on both sides.

The fingerprint detects accidental incompatibility; it is not authentication,
authorization or a collision-proof schema identity. Exact revisions are checked;
no arbitrary field insertion, schema evolution, negotiation or field skipping
is promised. Stable protocol IDs and field semantics remain developer-managed.
The transport must frame and correlate responses; no multiplexing ID, session,
retries, cancellation, network I/O or capability security is implemented here.

The codecs cover fixed-width LE signed/unsigned integers, strict bool, binary64,
bounded binary strings, arrays, bounded sequences, optional values and existing
records. Values are owning. Type declarations, field order and bounds contribute
to the wire contract. Field-ID order is strict. No C++ object layout or raw pointer
serialization exists. Text encoding and remote object/callback capabilities are
not silently treated as binary value conversion.

Public decoding stages values and commits only after full validation. Handlers
are invoked only after a successful full request decode. Trusted record constructors
and custom codecs are not a security sandbox. Variable-size allocation/element/depth
budgets are checked, but these are not total RSS/stack/in-flight budgets. Encode
and decode limits are independent. OOM/throw behavior follows the documented policy;
no rollback or exactly-once business side effect guarantee is made.

## FF/Portal integration status

The new async request owns its encoded bytes. A transport accepts ownership and
returns `async_submission`; an accepted request must make exactly one terminal
owning callback (inline is allowed), while rejection/throw-before-acceptance
must retain no callback and never invoke it. The response is checked and decoded
into owning storage before Portal completion. The original completion token's
local owning pin is retained, including callback self-destruction safety.

There is no extra shared_ptr operation state, std::function or per-call blueprint
created by the adapter itself. Portal's existing `static_invocation` allocation
and mutex remain; FF's awaitable remains when FF is used. No speculative state
fusion or unsafe early recycling is introduced. The API returns a concrete Call
that the existing `portal::ff::await_static_invocation<Call>()` can consume over
a reused blueprint. The supplied full-checkout FF gate requires actual FF headers
and has no stub. **It was not executed here.** There is no FF performance number.

## Validation actually executed

- GCC14.2 C++14 standalone CTest: **14/14 passed**.
- Clang17 C++14 standalone CTest: **14/14 passed**.
- All three new headers compile independently with both compilers, warnings as errors.
- Both strict/relaxed no-exception codec and actual Portal async test variants passed.
- Six negative compile tests reject duplicate IDs, mismatched member types,
  missing schema, implicit narrowing, wrong native return type and vector<bool>.
- Independent Python `struct` encoding verifies exact golden request/record bytes
  and signature fingerprints, rather than deriving expected bytes from C++ helpers.
- Codec tests include every truncated prefix, trailing bytes, header mutations,
  schema mismatch, bounded allocations/elements/depth, same-vector request/reply,
  nested sync calls, owning results after cache release, and 20,000 malformed mutations.
- Four threads with independent workspaces complete 20,000 scalar calls.
- Actual Portal async tests exercise inline, deferred, rejection, throw-before-accept,
  cross-thread completion, 200 submit/finish races, owned input snapshots, transport
  lifetime after external-owner release and destruction of the final token callback.
- Allocation-failure injection covers multiple preparation/response points and
  verifies reuse recovery. A valid first field plus invalid second field does not
  invoke the native business function.
- GCC **ASan + UBSan + LeakSanitizer** pass codec, async and allocation-failure tests
  (`detect_leaks=1`). GCC **TSan** passes codec concurrency and async race tests.

No full repository checkout, generated-binder integration build, FF gate, MSVC,
ARM, external production workload, or Python/Node runtime regression suite was
run. This is test evidence, not a formal safety proof. Full-parent-repository CI
is not modified or implicitly run by adding this standalone directory.

### Why the local legacy benchmark is explicitly labeled isolated

The container could not download GitHub archives/clones. Source was retrieved
through the GitHub connector. Current `rpc.h`, `async.h`, `optional.h`, `inplace.h`
and `error_policy.h` bytes were reconstructed and checked against their canonical
Git blob SHAs. The accepted `rpc_reuse.h` and extraction tool come unchanged from
the prior user-applied reuse patch. The codec benchmark extraction removes only
unavailable unrelated backend declarations and supplies the traits needed for
legacy binders. Full-checkout benchmark mode uses real backend headers by default.
The new codecs and actual async state implementation were not replaced by stubs.

## Reproduction and retained evidence

Start with `docs/typed_rpc_experiment.md` and `benchmarks/typed_rpc/README.md`.
Run the full-FF gate with `PORTAL_TYPED_RPC_REQUIRE_FF=ON` and a complete FF checkout
before claiming current-pair compatibility. Record deployed compilers and CPUs
before adopting these measurements as a capacity prediction.

Evidence files: `provenance.json`, `validation/`, `initial/`, `factors/`,
`selected/`, `allocator-control/`. Timing binaries do not interpose allocation;
separate allocation binaries do. Every process output, ordering and checksum is
retained. Min–max are between-process observations, not confidence intervals.

The patch is additive against the stated baseline and has no automatic default
activation. The useful conclusion is that a checked typed representation path
can avoid material work still present in a well-optimized dynamic backend. It is
not evidence that serialization, ownership or scheduling have become free.
