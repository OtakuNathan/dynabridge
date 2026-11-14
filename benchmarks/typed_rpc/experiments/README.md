# Reverse factor patches (not production defaults)

These patches apply to the selected implementation added by this experiment.
Use a disposable checkout; do not apply them to the legacy Portal baseline.

- `resize_both.patch`: string resize+copy, and clearing cached byte vectors.
- `assign_string.patch`: selected string assignment, but clearing byte vectors.
- `retain_bytes.patch`: selected retained byte size, but string resize+copy.
- The selected production experiment combines string assignment and retaining
  initialized byte elements. Its original five-round factor binary/source hashes
  are in `../results/2026-11-14-sandbox/factors/provenance.json`.

The patches also retain any exact source-level assertion changes needed to
reconstruct the recorded factor headers. No factor was promoted solely from
allocation counts. Full raw outputs, including regressions, are retained.

Example with an otherwise clean checkout containing the typed-RPC patch:

```sh
git apply --check benchmarks/typed_rpc/experiments/resize_both.patch
git apply benchmarks/typed_rpc/experiments/resize_both.patch
# Configure into a new build directory; build before timing, never during timing.
```

The control patches target the public header paths. Historical result hashes
refer to the original experimental snapshot; reruns produce new provenance.
