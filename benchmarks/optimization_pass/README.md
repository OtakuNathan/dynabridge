# Portal boundary-cost pass

This directory keeps the matched probes and raw evidence for the November 2026
pass. They measure Portal's own call-boundary costs; they do not establish a
production transport capacity, tail-latency SLA, or a framework ranking.

## Reproduce

Use two source trees, one at the baseline commit
`7a6f53bbc30d3b4159e36de138486bffe90f3e62` and one at the candidate. The runner
compiles **the same benchmark source** against each tree, using C++14, `-O3`,
`-DNDEBUG`, the same runtime headers/libraries and the same optional FF checkout.
It currently supports Linux with GCC or Clang and the active Python embedding
library. Node families need the matching official Node headers.

```sh
python3 benchmarks/optimization_pass/run.py \
  --baseline /absolute/path/to/baseline \
  --out /absolute/path/to/results \
  --node-include /absolute/path/to/node/include/node \
  --ff-include /absolute/path/to/fixed/flux_foundry \
  --build-only

# Finish all other builds/tests before this phase. Pinning is not isolation.
python3 benchmarks/optimization_pass/run.py \
  --baseline /absolute/path/to/baseline \
  --out /absolute/path/to/results \
  --cpu 0 --pairs 7 --run-only

python3 benchmarks/optimization_pass/summarize.py /absolute/path/to/results
```

Use `--families core,rpc` to omit both language runtimes, or any subset of
`core,core_threaded,rpc,python,node,constructor,tcp`. Supply the same family selection to both
phases. `--ff-include` is optional; it adds the FF TCP integration control, and
must refer to the same dependency for both variants. The runner never compares
one Portal revision using an older FF with another using a newer FF.

The default sample has one million core/small RPC/Python calls, 100,000 Node
calls/constructions, and 20,000 local / 2,000 TCP calls in the TCP control.
Larger payload cases reduce their iteration counts, recorded in the raw output.
`--iterations`, `--runtime-iterations`, `--node-iterations` and
`--tcp-iterations` override these counts. Keep Node counts at or below two
million to preserve exact integer checksums for wide unsigned values.

All binaries are built before measurement. Each sample runs in a fresh process;
before/after process order alternates between pairs. The constructor probe
retains objects through post-timing validation and explicitly drains Node
finalizers between cases. Other runtime probes include their normal runtime
allocation/GC behavior. TCP threads inherit the selected CPU affinity; this is
a loopback regression control, not a multicore networking throughput test.

## What each probe establishes

| Probe | Scope | Important limits |
| --- | --- | --- |
| `core_probe.cpp` | Static/erased invocation with inline/deferred completion; direct overload binders with owning 64 KiB values | Deferred completion runs on the same thread after submission. This does not measure cross-thread handoff. Payload cases include result validation. |
| `core_threaded` | The same core binary, after creating and joining one thread before warmup | Exposes shared-pointer/mutex costs after the runtime has entered multithreaded mode. Calls are still completed on the measuring thread. |
| `rpc_probe.cpp` | Complete framed in-memory loopback, arities 0/1/2/4/5/8 and strings 16 B/1 KiB/64 KiB | Includes request/response frames, validation and conversion. No sockets, server queue or multiplexing. |
| Existing Python benchmark | Import/export, objects, callbacks, overloads, constructors and bound methods | CPython-specific results; raw C API rows provide controls. |
| Existing Node benchmark | Import/export plus strict numeric/UTF-8 conversions | JS loop and runtime costs are included. The broad constructor case differs from the isolated retention probe. |
| Existing Node constructor benchmark | Construction with objects retained through validation, plus ordinary export control | Construction excludes finalization/teardown; that cost is not eliminated. |
| Existing TCP benchmark | Local TCP and optional FF handoff regression controls | Results depend heavily on loopback scheduling. No claim about deployment capacity. |

Core and RPC allocation/copy counters run in a separate 100-call sample and are
disabled during timing. The runtime structural API-count assertions live in
the tests, not the timed addons. Baseline and candidate must return matching
checksums; failures abort the run.

The runner records commands, source hashes, binary hashes, runtime versions,
affinity, process ordering and all stdout/stderr. The summarizer checks paired
cases and checksums, and emits every sample plus variant medians, full ranges,
and the number of individually faster pairs. It does not remove outliers or
treat a median alone as evidence of a reliable gain.

The recorded pass also uses `run_matrix.py` for isolated implementations and
`node_focus.js` for longer warmed Node call loops. The selected default excludes
the Python/Node performance and RPC inline-storage experiments; their initial
and factor-study results remain archived. The final RPC acceptance phase is
separate. See the [patch guide](experiments/README.md) to reconstruct controls.

See [the measured report](results/2026-11-13-vm/report.md) for the exact environment,
observations, correctness fixes, rejected ideas and remaining costs.
