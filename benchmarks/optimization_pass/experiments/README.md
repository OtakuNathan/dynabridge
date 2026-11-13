# Recorded implementations

These patches apply individually to the frozen baseline
`7a6f53bbc30d3b4159e36de138486bffe90f3e62`. They preserve the implementations
actually tested, including candidates excluded from the library. Their status
and observations are in the [report](../results/2026-11-13-vm/report.md).

| Patch | Scope / status |
| --- | --- |
| `core-final.patch` | Selected completion pin, cleanup consolidation and direct-binder moves |
| `async-pin-only.patch` | Corrected async control with the original four-lock cleanup |
| `rpc-writer-byte-only.patch` | Selected wide-field batching and single-byte fast path |
| `rpc-writer-only.patch` | Initial batch-all-widths writer experiment |
| `rpc-storage-only.patch` | Rejected four-slot decoded argument storage |
| `rpc-full.patch` | Superseded combined RPC candidate from the first seven-pair run |
| `rpc-full-byte-fastpath.patch` | Combined storage/writer candidate with byte fast path and explicit wide-index accessor |
| `runtime-python-reset.patch` | Selected Python reference-reset correctness fix |
| `runtime-python-offset-only.patch`, `runtime-python-typed-only.patch` | Rejected performance changes individually, without the independent reset fix |
| `runtime-python-reset-offset.patch`, `runtime-python-reset-typed.patch` | Isolated Python factors used in the five-round study |
| `runtime-python-runtime-full.patch` | Superseded full measured Python combination |
| `runtime-node-constructor.patch`, `runtime-node-converters.patch` | Rejected isolated Node factors |
| `runtime-napi-runtime-full.patch` | Superseded full measured Node combination |
| `runtime-runtime-candidate-probes.patch` | Candidate-specific structural test probes; not required by the selected default |

Full and partial patches affecting the same header are alternatives. Do not
stack them. For example, recreate the initial combined production headers by
applying `core-final.patch`, `rpc-full.patch`,
`runtime-python-runtime-full.patch`, and `runtime-napi-runtime-full.patch` to
one clean baseline tree. The selected headers instead use `core-final.patch`,
`rpc-writer-byte-only.patch` and `runtime-python-reset.patch`.

```sh
git -C /path/to/baseline apply --check /path/to/selected.patch
git -C /path/to/baseline apply /path/to/selected.patch
```

Compile the benchmark sources from this pass against each reconstructed header
tree with identical flags. Exact build commands and header/binary hashes are in
the recorded [RPC](../results/2026-11-13-vm/rpc-variant-builds.json),
[runtime](../results/2026-11-13-vm/runtime-variant-builds.json), and
[async control](../results/2026-11-13-vm/core-pin-control-builds.json) manifests.
Their absolute paths identify the original workspace; substitute your own
baseline, output, Python and Node-header paths when reproducing elsewhere.

The [factor configuration](../results/2026-11-13-vm/isolation/matrix.json)
records all run commands and environments. Build every variant first, then use
`run_matrix.py` with that configuration's `config` array to repeat the rotating
order. The selected implementation can also be measured directly with
`run.py`; see the parent [instructions](../README.md).
