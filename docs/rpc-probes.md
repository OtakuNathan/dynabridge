# RPC-only export probes

The RPC export backend opts into two-phase argument conversion with:

```cpp
using export_probe_policy = dynabridge::rpc_export_probe_policy;
```

A custom RPC-like backend must opt in explicitly. Sharing `rpc::value` or
inheriting its converters alone does not opt in. Python, Node and other backends
keep their existing conversion and overload behavior. `from_optional`,
`from_cast`, import/result decoding and public binder calling signatures are
unchanged.

## Input converter contract

Every scalar input converter used by an opted-in export must add:

```cpp
template<class Context>
static bool probe(Context&, const dynamic_value_t& value) noexcept;
```

`probe` must return exactly `bool`. It must not consume the value, allocate a
full native representation, change user-visible state, or invoke callbacks.
For stable inputs it must accept exactly what `from` accepts. A false probe is
a mismatch. Allocation/API failures are errors, not overload mismatches. With
exceptions enabled a probe may throw; strict no-exception mode requires it to
be `noexcept`, like the existing `from`/`to` contract. Output-only converters and
converters used only by `from_optional` do not require probes.

Candidates remain in declaration order. Arity is checked first, then all needed
probes finish before **any scalar conversion** for that candidate starts. Probes
may short-circuit on mismatch. Only the first accepted candidate converts and
invokes. Each scalar `from` runs once. A conversion that rejects after an accepted
probe raises `bad_conversion` (asserts/aborts without exceptions), with no retry
of another overload. Native and conversion exceptions are likewise terminal.
This is an all-or-nothing **selection** guarantee, not a rollback transaction:
allocation failures or custom converter side effects are not rolled back.

Committed conversions may consume their own input but must not invalidate
other pending arguments or referents. Concurrent mutation is unsupported.
Custom mutable accessors must not alias a consumable value into several slots.
Explicit `std::ref` retains existing aliasing semantics; violating probe
stability is terminal, never permission to retry on already-consumed inputs.

Custom scalar `export_argument` specializations need a boolean `probe` too.
Object/callable descriptors retain their `try_make`/`get` contract. On opted-in
backends descriptor handles are prepared once, before scalar probing, and
moved into the successful conversion. Failed candidates release owned handles;
borrowed native referents must remain alive throughout the callback. Descriptor
preparation can have external effects and is not claimed to be a pure probe.
Non-opted backends retain the old descriptor evaluation path.

## Ownership and compatibility

The standard RPC router owns its decoded request vector. Its internal mutable
accessor is used only for exact library single-signature or overload binders
whose backend opted in. Duck-typed/custom binders keep their old const accessor
or typed call path. No ownership permission is inferred from the dynamic value
representation, a method name, or a custom binder's nested aliases.

Direct binder calls still copy ordinary caller lvalues into their existing
tuples. Const vectors/accessors still copy owning strings. Owned router strings
move only after a complete candidate match. Borrowed strings copy when an owning
`std::string` is needed. Native value, `const&` and `&&` arguments remain valid
through the native invocation; callers must not retain references afterward.
All RPC wire kinds and ranges, including nonfinite doubles, are unchanged.

## Capstan

Capstan opts in its independent `dataset_backend`, adds
`uint64_t` and `summary_t` probes, and validates encoded summaries directly from
a byte view. Its manual member binder automatically uses the explicit policy.
Its public `try_dispatch` still accepts a const vector, so arguments retain
their existing ownership there.

## Tests and separate fixes

`rpc_probe_test` covers declaration order, failed-candidate preservation,
conversion counts, move-only lifetime, values/references, borrowed strings,
legacy backends without probes, custom binders, descriptor cleanup and terminal
errors. It also runs in both no-exception modes. Configure checks reject missing,
wrong-return and strict throwing probes. Existing Python/Node tests exercise the
unchanged path. Return ownership and Node/Python exception/lifetime regressions
also run in the combined test suite.
