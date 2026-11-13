# Portal boundary-cost pass — 2026-11-13

## Selected implementation

This pass keeps three performance changes and two required lifetime fixes:

1. Async delivery/submission claims cleanup while it already holds the transition mutex. Normal inline and deferred success use two lock acquisitions instead of four. A completion token also pins its state locally while invoking callbacks, fixing a reproduced use-after-free.
2. Direct overload binders move their already-owned local argument values and required return values when safe. `std::move_if_noexcept` preserves the copy fallback for custom copy-only or potentially throwing-move values.
3. RPC writes each wide integer field with a grouped little-endian append. Single-byte fields keep `push_back`.
4. Python borrowed-reference reset retains the replacement, publishes it, then releases the previous object, fixing self-reset and finalizer re-entry.

The larger RPC inline-storage experiment and the Python/Node performance experiments were tested and removed from the default. Their patches, code-size costs, positive results and regressions are retained below. The final Node benchmark addon is **byte-identical to the baseline**; the final Python benchmark is **byte-identical to the reset-only control**. See [selected provenance](selected-provenance.json).

Baseline: `7a6f53bbc30d3b4159e36de138486bffe90f3e62`.

## Final RPC acceptance results

These are complete framed in-memory round trips, including argument conversion, request encoding, dispatch, result encoding and decoding. The **final selected implementation** was rebuilt and measured in seven alternating pairs after the factor study. Both variants use the same probe source. All checksums match.

Numbers are medians of per-process average ns/call, with lower values better. They are not percentiles of individual requests or a network-capacity measurement.


| Case | Before ns/call | After ns/call | Change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| scalar_0 | 182.535 | 172.787 | -5.3% | 5/7 |
| scalar_1 | 273.917 | 220.035 | -19.7% | 7/7 |
| scalar_2 | 448.485 | 284.271 | -36.6% | 7/7 |
| scalar_4 | 500.919 | 385.670 | -23.0% | 7/7 |
| scalar_5 | 584.533 | 383.506 | -34.4% | 7/7 |
| scalar_8 | 743.785 | 499.818 | -32.8% | 7/7 |
| string_16 | 438.297 | 370.884 | -15.4% | 5/7 |
| string_1024 | 584.964 | 535.029 | -8.5% | 6/7 |
| string_65536 | 46,335.979 | 43,832.952 | -5.4% | 4/7 |


The scalar cases with 1, 2, 4, 5 and 8 arguments improve in all seven pairs. The 64 KiB result is only a 4/7 split with overlapping sample ranges; its median is recorded, but a reliable large-payload speedup is not claimed. Complete ranges and observations are in [acceptance/summary.csv](acceptance/summary.csv) and [acceptance/samples.csv](acceptance/samples.csv).

The selected change does **not** remove RPC allocations. Scalar calls with arguments still perform three allocations, zero-argument calls two, and these string echoes five. The earlier 3→2 and 5→4 reductions belong to the rejected inline-storage candidate. Large echoes retain four payload copies across caller/request, decoded owning value, response and returned owning value.

## C++ core results

These core production headers are unchanged since the initial seven-pair measurement. The benchmark has a separate 100-call allocation/copy sample; counter writes are disabled during timing.


| Case | Before ns/call | After ns/call | Change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| static_inline | 78.608 | 67.061 | -14.7% | 6/7 |
| static_deferred | 82.048 | 59.982 | -26.9% | 7/7 |
| erased_inline | 108.991 | 84.605 | -22.4% | 7/7 |
| erased_deferred | 110.377 | 95.422 | -13.5% | 5/7 |
| direct_overload_64k | 35,640.849 | 3,698.344 | -89.6% | 7/7 |
| optional_overload_64k | 5,872.695 | 3,758.486 | -36.0% | 7/7 |


The 64 KiB cases exercise **direct overload-binder invocation with an owning dynamic value**. Required invocation drops from three dynamic copies/allocations to one; optional invocation drops from two to one. Ordinary caller lvalues remain intact. The result is validated against the input in the timed payload cases, and every case verifies its checksum.

This large gain does not describe the normal RPC round trip: the RPC router already invokes `dispatch_optional` through its own accessor. Static async calls still allocate one operation state; erased calls still allocate two objects in these probes. Lock reduction, rather than allocation removal, is the async mechanism.

### Multithreaded-process cost and corrected control

A separate process mode creates and joins a thread before warmup, then performs the same inline/deferred calls on the measuring thread. The local glibc diagnostic changes `__libc_single_threaded` from 1 to 0. This exposes the runtime's multithreaded ownership costs; it does not measure cross-thread scheduling or handoff latency.

Compared with the original, unsafe baseline, the net result is mixed:


| Case | Before ns/call | After ns/call | Change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| static_inline | 81.771 | 96.360 | +17.8% | 4/7 |
| static_deferred | 86.447 | 92.384 | +6.9% | 4/7 |
| erased_inline | 131.186 | 140.804 | +7.3% | 3/7 |
| erased_deferred | 120.587 | 113.181 | -6.1% | 4/7 |


The local `shared_ptr` pin is required: without it, a continuation can destroy the last completion token and free the state while delivery is still executing. Removing that ownership would restore a reproduced bug. Checking `use_count()` or a submission flag cannot establish an owner that remains alive through concurrent/reentrant callbacks.

To separate this correctness cost from the cleanup optimization, a five-round follow-up compares the full implementation with a **pin-only corrected baseline** that retains the original four-lock cleanup:


| Async case | Corrected control | Selected cleanup | Change | Faster rounds |
| --- | ---: | ---: | ---: | ---: |
| static_inline | 102.559 ns | 90.987 ns | -11.3% | 4/5 |
| static_deferred | 106.093 ns | 90.251 ns | -14.9% | 4/5 |
| erased_inline | 176.478 ns | 132.246 ns | -25.1% | 4/5 |
| erased_deferred | 136.650 ns | 140.747 ns | +3.0% | 4/5 |


The first three corrected-control medians improve, each with four faster rounds out of five. Erased deferred completion remains mixed. These observations support removing redundant locking, but do not establish a blanket net improvement over the unsafe original under every process/thread configuration. Both tables remain part of the evidence.

## Reproduced lifetime defects

### Completion callback destroys its final token

A stored completion callback owns the last token. Its handler clears that callback, freeing the operation state before the old `deliver()` reaches `cleanup()`. The frozen baseline reproducer reports an AddressSanitizer heap-use-after-free. `completion::finish()` and `detach()` now copy the owning pointer before entering delivery, preserving state lifetime through arbitrary handler/resource destruction.

Regression coverage includes callback self-destruction, detach followed by nested finish, moved-from handlers that retain captures, access/operation/context destructor re-entry, inline completion before submission returns, and 200 finish/detach/submit-return races.

- [Minimal reproducer](../../repros/async_final_token.cpp)
- [Baseline ASan evidence](validation/baseline-final-token-asan.log)
- [Core validation commands](validation/core-validation.txt)

### Python reset releases an aliased borrowed replacement

`ref.reset(ref.get())` with one owning reference previously decremented the object to zero before incrementing that same pointer. With `PYTHONMALLOC=malloc`, the baseline reports an AddressSanitizer heap-use-after-free in `Py_INCREF`. A separate deallocator observer also demonstrated that replacement finalization exposed the stale old pointer.

`reset()` now retains a borrowed replacement first, publishes it with `std::exchange`, and decrements the previous object afterward. A reentrant reset performed by the finalizer remains installed. Adopted/owned policy still requires a distinct owned reference, including when the incoming pointer equals the current pointer.

- [Minimal reproducer](../../repros/python_reset.cpp)
- [Baseline ASan evidence](validation/python_reset_baseline_same.log)
- [Independent observer evidence](validation/python_reset_baseline_observe.log)
- [Runtime validation history](validation/runtime-validation.txt)

## Experiments removed from the default

### Inline decoded RPC arguments

Four local owning slots removed one allocation for small calls. The standalone object grew from a 24-byte vector to a 328-byte inline-storage object on this ABI. Legacy/custom binders still needed their exact owning-vector/accessor path, adding an internal move when materialized from the inline slots.

Allocation savings did not reliably translate to time savings. In the isolated storage-only factor, five- and eight-argument calls regressed 12.2% and 8.6%, with zero faster rounds out of five, despite those calls still using heap storage. Strings also lacked a convincing benefit. The generic accessor/stack/allocator changes introduce costs beyond the allocation counter. The final code therefore retains the original owning decoder and router.

The first batched-writer prototype also routed one-byte tags through an out-of-line generic range-insertion helper. Disassembly established the extra calls; it did not prove all wall-time changes were caused by them. A byte fast path was then tested, and the final acceptance comparison uses **wide-field batching with the original argument storage**.

### Python vectorcall specialization and offset slot

A holder-specific vectorcall entry removes an erased dispatch hop, but the old entries remain emitted alongside specialized copies. In the isolated benchmark, `.text` grows from 40,614 bytes for reset-only to 46,438 bytes for reset-plus-specialization, a 14.3% increase. This is a verified code-generation cost; instruction-cache effects are a hypothesis, not an established cause.

The first combined candidate regressed the export-constructor median 28.6%. In the isolated follow-up the combined constructor median was 211.0 ns versus 148.3 ns for reset-only, a 42.3% increase. Some isolated export/vectorcall rows improved, while controls and other paths moved materially. The offset-only bound-method case also failed to reproduce a stable advantage. Neither performance change is enabled; the ownership fix remains.

### Node callback-info and conversion changes

Constructor argument collection, strict numeric checking and UTF-8 size/type probing all had plausible operation-count reductions. Tests covered exact arity, numeric limits, NaN/infinities/fractions, Unicode/NUL behavior, API error versus mismatch, JS exception identity and reentrant construction. The strong-reference removal experiment from September was never re-enabled.

The broad first run had attractive numeric/string medians, but warmed follow-up loops and isolated construction did not establish a reliable overall gain. The full callback case regressed 14.6% in the focused follow-up, with one faster round out of five; construction's initial median gain did not repeat. Unchanged raw controls were also noisy. The evidence does not isolate every cause, and does not justify enabling these default changes. Final N-API headers and the matched addon binary are identical to baseline.

The useful semantic regressions remain. API-count/offset-slot assertions that required the rejected implementations were removed. The N-API stub retains the documented string-mismatch status and undefined-filling behavior.

## Validation of the selected implementation

- Frozen baseline: 35/35 local Release tests passed.
- Selected implementation: 44/44 local Release and 44/44 Debug tests passed, including real Python and Node runtime/worker tests.
- Both no-exception modes (`PORTAL_NO_EXCEPTION_STRICT=0` and `1`) are covered, alongside the configure-time positive/negative contract checks.
- Core lifetime/concurrency tests passed ASan+UBSan and TSAN. Selected Python ownership tests passed ASan+UBSan with `PYTHONMALLOC=malloc`.
- RPC codec/ownership tests include exact wire bytes, every truncated prefix, invalid lengths/counts/tags, output preservation, legacy binders, nested calls and concurrent dispatch. Final sanitizer commands/results are retained with the validation records.
- The installed C++14 package consumer built and ran. Separate integration checks also passed with the completed FF branch `0d7574c5c988eb560060771257b485b1f967cd29`; those checks are separate from the fixed-dependency performance samples.
- CI build parallelism is bounded at two compiler jobs. Cross-platform CI has not run for this pass yet: publication was blocked by GitHub with `403: Resource not accessible by integration`. The current GitHub App installation lists `flux_foundry` but does not include `portal`. The local validation above is complete; a Portal pull request and its cross-platform checks still require repository access.

LeakSanitizer cannot operate under this environment's ptrace setup. Address/UB sanitizer runs use `detect_leaks=0`; address/UB checks remain enabled. This is not a local leak-validation claim. The repository's separate Valgrind CI remains available.

## Environment and method

- Linux x86-64, AMD EPYC 7763, GCC 14.2, C++14, `-O3 -DNDEBUG -pthread`; Python 3.12.14; Node 24.22.1 with matching official headers.
- Measurements pin the process to CPU 0 within the available 0–8 mask. Pinning is not isolation; this is a shared VM with no controlled frequency/thermal envelope.
- Every phase completes compilation and tests before measurement. No agents run parallel timing or compilation during a timing phase. Small progress/provenance reads are not a guarantee of an otherwise idle host.
- Identical benchmark sources and compiler flags are used for paired implementations. Initial and acceptance studies alternate process order; the five-round factor study rotates/reverses variant order.
- Core and small RPC probes use one million iterations per process; payload probes reduce counts. The corrected async control and Python factor study use two million; focused Node loops use one million, with separate smaller construction/large-string counts. Exact commands and counts are retained.
- Initial Python/Node/FF-TCP comparisons belong to the superseded broad candidate. Final RPC was measured again after selecting the narrow change. No final network throughput gain is claimed.
- FF was fixed at `a93d246981a49648949c489a1918f5ccb61ed15d` in both sides of integration benchmarks. FF optimization gains are not attributed to Portal.

## Evidence and reproduction

[Runner instructions](../../README.md) explain the two build/run phases. [run.py](../../run.py) reproduces matched baseline/final probes; [run_matrix.py](../../run_matrix.py) executes recorded prebuilt factor configurations. Every sample, failure-free process output, command, ordering, binary hash and header hash is retained. No timing outlier was discarded.

| Evidence | Contents |
| --- | --- |
| [Initial manifest](initial/manifest.json), [raw process output](initial/raw_outputs.jsonl), [all samples](initial/samples.csv), [summary](initial/summary.csv) | The full first candidate, including regressions; 98 serial processes |
| [Isolation matrix](isolation/matrix.json), [all samples](isolation/samples.csv), [summary](isolation/summary.csv) | Corrected async control and RPC/Python/Node factors; 95 serial processes |
| [Acceptance manifest](acceptance/manifest.json), [raw output](acceptance/raw_outputs.jsonl), [all samples](acceptance/samples.csv), [summary](acceptance/summary.csv) | Selected RPC; 14 serial processes |
| [Selected provenance](selected-provenance.json) | Final production header hashes and binary-equivalence checks |
| [Environment](environment.json), [runtime code sizes](runtime-code-size.json) | Runtime/toolchain details and measured emitted-code sizes |
| [Experiment patches](../../experiments/README.md) | Baseline-applicable candidates, corrected controls and selected changes |

Raw per-process stdout/stderr are consolidated into JSONL for the published evidence; their original paths and exact content are preserved. `summarize.py` reads either consolidated archives or fresh individual log files. Re-running it checks case pairing and checksums and reconstructs the tables.

Blank context lines in archived patches and trailing horizontal whitespace in
compiler/diagnostic text are normalized for repository formatting. The
[normalization record](format-normalization.json) retains before/after hashes;
all published patches still apply cleanly. Timing process outputs in JSONL are
unchanged.

The measured gains concentrate on redundant core work and scalar framing. Further large-payload or deployment gains require workload-specific decisions about buffer ownership/reuse, runtime objects, scheduling and transport. This pass does not weaken those contracts to obtain a smaller microbenchmark number.
