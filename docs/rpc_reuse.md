# Explicit RPC storage reuse (C++14)

This is an opt-in synchronous codec/router path. Existing imported delegates,
`rpc_backend::invoke_impl`, `router::dispatch`, the owning checked decoder and
`loopback_transport` do **not** start caching implicitly. The wire format and
ordinary binding/conversion code are unchanged.

## Server: reuse argument-vector and caller-owned reply storage

```cpp
#include <portal/portal.h>
#include <portal/backends/rpc_reuse.h>

// Register all bindings first. Each worker owns a separate workspace.
portal::rpc::dispatch_workspace decode_storage(64);
portal::rpc::bytes reply;
// server is a registered portal::rpc::router; request is a complete frame.
server.dispatch_into(request, reply, decode_storage);
```

`dispatch_into` decodes owning `rpc::value`s, validates the whole frame before
invoking a binder, and encodes into `reply`. Clearing a request destroys all
payload values; only the **empty** argument-vector allocation may be cached.
The input and output may be the same `bytes` vector because decoding finishes
before output is changed and decoded strings own their storage.

`reply` belongs to the caller. A network sender may take ownership of it; do not
modify or recycle that buffer while an asynchronous send is still using it.
Reusing the capacity is useful only when its previous consumer has finished.
This API does not implement a transport, framing, backpressure or cancellation.

## Client: explicitly reuse request and response buffers

```cpp
#include <portal/portal.h>
#include <portal/backends/rpc_reuse.h>

portal::rpc_backend::export_context_t context;
portal::rpc::router server;
server.define("add", portal::create_export_callable_binder<int(int, int)>(
    context, [](int a, int b) noexcept { return a + b; }));

portal::rpc::reusing_loopback_transport transport(server);
portal::rpc::call_workspace buffers(64 * 1024);
const auto method = portal::rpc::detail::symbol_id("add");
auto answer = portal::rpc::round_trip_reusing(
    transport, buffers, method,
    portal::rpc::value::signed_integer(20),
    portal::rpc::value::signed_integer(22));
// answer.as_signed() == 42; answer owns any decoded string storage.
```

This deliberately low-level entry point takes already-lowered RPC values. It
is not an automatic replacement for generated import delegates. An application
may lower values with its existing converters before calling this entry point;
those conversions have their own ownership/error contracts and costs.

A custom transport must provide:

```cpp
void round_trip_into(const portal::rpc::bytes& request,
                     portal::rpc::bytes& response);
```

It must finish reading `request` and fully write `response` before returning,
and must not retain either reference. A returned response is fully decoded into
an owning value before the buffers are returned to the workspace. A transport
that completes later needs **operation-owned buffers** instead; using this
synchronous helper for such a transport creates a lifetime error.

## Confinement, reentrancy and bounds

Workspaces and `reusing_loopback_transport` are not internally synchronized.
Use one per worker, or supply external synchronization; they must outlive every
active call that uses them. A router can still be shared under its existing
registration contract, and its bound callables must themselves support the
chosen concurrency. Shared mutable user data does not become safe through this
API.

Each active scope takes ownership of its buffers/vector by swapping with the
empty cache. A nested **synchronous** call therefore gets different active
storage. On return, at most one eligible empty allocation per cache is kept;
the others are destroyed. This supports recursion, not concurrent sharing of
one workspace, and does not promise allocation-free nested calls.

`call_workspace(max_retained_bytes)` bounds **capacity per cached byte buffer**.
The default is 64 KiB for each of request and response. The argument workspace's
default is 64 `value` slots. These limits bound idle retention, not active input
size, recursion depth, total live memory or allocator-internal retention. A
64-KiB payload plus wire headers exceeds the default 64-KiB byte-buffer budget.
Choose a budget for the whole frame, not just the payload. The recorded timing
probe explicitly used a 1-MiB budget per byte buffer; it did not change defaults.

`release_cached_storage()` releases idle caches, not active scopes. Active
scopes can repopulate a cache when they return. No TLS cache, global pool or
allocator-process setting is installed. `clear()` is not a secure memory wipe.
Set frame-size/admission limits separately for untrusted input.

## Errors and compatibility

Existing `try_decode_request` preserves its output on malformed input and is
untouched. The new internal scratch decoder may leave a partial request, but
its enclosing lease always clears it. No binding is invoked before structural
validation succeeds. Conversion, unknown-method and exception errors retain the
ordinary router's wire-error format and ordering.

Allocation/transport exceptions can discard reusable capacity; the workspaces
remain valid for subsequent calls. Allocation failure during error encoding may
still escape, as with the original path. This is **not** a rollback or exactly-
once guarantee for business side effects after a callable has started.

No borrowed string is passed to a custom binder by the new decoder. A custom
binder copying an owning decoded value can retain that copy after dispatch.
Low-level `detail::encode_*_into` output must not alias borrowed string input.

## Validate before adopting

Run the standalone full-checkout gate (normal mode uses the actual generated
binder and backend headers):

```sh
cmake -S tests/reuse_pass -B build-reuse -DCMAKE_BUILD_TYPE=Release
cmake --build build-reuse --parallel 2
ctest --test-dir build-reuse --output-on-failure
```

This directory is independent of the root CMake test list; add it to the desired
CI workflow explicitly. The recorded sandbox tests used an isolated codec/router
extraction because only a hash-verified source subset was available. They do
**not** establish full generated-binder, FF, Python/Node or cross-platform
integration. A full-checkout generated-binder gate is supplied but was not run
in that environment. See the [experiment report](../benchmarks/reuse_pass/results/2026-11-14-sandbox/report.md).
