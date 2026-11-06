# Node constructor experiment — 2026-09-08

Status: measured candidate, **not enabled in the library**. The experimental
implementation is preserved in
[`napi-constructor-no-ref.patch`](../benchmarks/experiments/napi-constructor-no-ref.patch).

## Change under test

Export construction currently creates a temporary owning `object_t`, which
creates and immediately deletes a strong N-API reference. The candidate adds a
backend construction hook with the existing object-wrapper behavior as its
default. Node overrides it to attach native state directly to the JS receiver
already rooted by the constructor callback. Type tagging, strict argument
conversion, wrap error cleanup and finalization remain in place. Explicit C++
object handles retain their existing persistent references.

## Environment and method

- Baseline commit: `0762d81175a6f3172b4a142b026d8392150fca6f`.
- Cortex-A72, aarch64, GCC 14.2, Node 24.19.0, Release, `-O3 -DNDEBUG -std=c++14`.
- node-addon-api was not available in this configuration; no comparison against
  that library is claimed here.
- Five runs per variant, alternating before/after and after/before order,
  separate Node processes, 100,000 constructions per case.
- Measurements were serial, without concurrent builds or test runs.
- A clean rebuild from a git archive reproduced the baseline binary exactly
  (SHA-256 `96ce8ec0950c92d5d3ae9c16017fd9802d8b29463fb0dbf673416e809c44f0ec`).

## Results

Medians in ns/op; lower is better. These are local microbenchmarks, not an
application-level speedup or a general framework ranking.

| Scenario | Before | Candidate | Change |
| --- | ---: | ---: | ---: |
| Existing full benchmark: dynabridge construction | 5193.1 | 4728.6 | -8.9% |
| Isolated construction, retaining objects through validation | 4597.0 | 4138.1 | -10.0% |
| Isolated benchmark: ordinary export control | 159.5 | 169.1 | +6.0% |
| CPU 3 pinned: construction | 4879.5 | 4429.1 | -9.2% |
| CPU 3 pinned: ordinary export control | 159.2 | 167.0 | +4.9% |

Existing construction samples, in each variant's run order:

```text
before: 4996.6 5094.5 5340.2 5193.1 5263.7
after:  4649.1 4553.8 4881.4 4738.6 4728.6
```

The isolated script allocates the retention array before timing, validates every
constructed object's value after timing, and drains GC/finalizers between cases.
Construction timings exclude teardown and validation. It also measures an
unchanged free-function export with 100,000 warmup and 2,000,000 timed calls.
The ordinary export slowdown also appeared in a separate export-only experiment
with 5,000,000 calls (medians 156.8 -> 167.1 ns).

The examined free-function callback assembly has the same instruction pattern
but different code addresses. Code layout is a possible explanation, not an
established cause. Since the ordinary-call regression repeated, the candidate
was removed from the default implementation rather than reporting only its
construction win.

## Frequency and temperature checks

The governor was `ondemand`; no governor or frequency settings were changed.
An initial 70-sample monitor observed two approximately 800 MHz readings but no
throttling flags. A follow-up used CPU affinity (`taskset -c 3`) and aligned
wall-clock timestamps from the timed regions with frequency samples:

- Three paired runs; 61 monitor samples, all approximately 1.800 GHz.
- Every timed region contained sampled readings at approximately 1.800 GHz.
- Temperature 44.3–47.2 degrees Celsius; every `get_throttled` reading was `0x0`.
- The ordinary export slowdown still appeared.

These samples do not explain the regression as thermal or frequency throttling;
they cannot rule out sub-sample fluctuations, scheduling interference, or other
microarchitectural effects. Monitor overhead also means this follow-up should
not be mixed with the unmonitored samples.

## Reproduction and validation

Build the baseline and candidate in separate checkouts with identical options,
applying the patch only to the candidate. Preserve each addon under a distinct
path, and alternate these commands:

```sh
DYNABRIDGE_BENCH_ITERS=100000 node --expose-gc \
  benchmarks/node_constructor_benchmark.js /absolute/path/to/addon.node
```

Use the existing `node_call_benchmark.js` for the broader scenario comparison.
The focused script emits `startMs` and `endMs` for environmental sampling.

The candidate passed 13/13 Debug and 13/13 Release tests, including a new
256-object retain/GC/release regression. The native Node stub test passed
Valgrind with zero errors and zero bytes left at exit. All benchmark targets
compiled; an installed C++14 package consumer built and ran. These checks
validate behavior, not the absence of performance regressions. The GC regression
and focused benchmark are retained independently of the experimental patch.
