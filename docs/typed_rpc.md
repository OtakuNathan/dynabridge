# Typed RPC

Typed RPC is a supported public C++14 module, available through the existing
`portal::portal` CMake target and installed headers. It supplies value codecs,
checked protocol-v1 envelopes, native dispatch, explicit storage reuse and an
owning asynchronous transport adapter.

| Header | Public API |
| --- | --- |
| `<portal/wire/schema.h>` | `portal::wire` codecs, limits, results and `PORTAL_WIRE_RECORD` |
| `<portal/rpc/typed.h>` | `portal::typed_rpc::method`, `router`, `workspace`, `call` and frame helpers |
| `<portal/rpc/typed_async.h>` | `make_rpc_invocation`, typed error category and owning async adapter |

## Choose an RPC style

Both RPC styles are supported and may be included in the same application.

| Need | Entry point |
| --- | --- |
| Existing Portal `.def` declarations, converters, first-viable overloads, custom binders or the existing tagged protocol | `<portal/backends/rpc.h>`; opt into storage reuse with `<portal/backends/rpc_reuse.h>` |
| Explicit shared value schemas, fixed method contracts and direct owning record/sequence codecs | `<portal/rpc/typed.h>`; use `<portal/rpc/typed_async.h>` for owned async calls |

Prefer typed RPC for new value-oriented protocols when both peers share the
schema. Keep the dynamic backend for its conversion/overload model or existing
peers. Each endpoint must choose the same wire protocol: `PTR1`/`PTS1` typed
frames and existing `DRPC`/`DRPR` frames are distinct. There is no implicit
protocol switch or negotiation. Existing generated APIs and protocol bytes keep
their existing behavior.

Transport implementations supply connection management, response correlation,
I/O, timeouts and shutdown. FF integration uses the existing optional invocation
adapter. Applications can use the codec and synchronous router without FF.

## Describe existing value members

```cpp
#include <portal/rpc/typed.h>

namespace w = portal::wire;
namespace rpc = portal::typed_rpc;
struct Packet {
    std::uint64_t sequence = 0;
    std::string payload;
};
#define PACKET_FIELDS(X, T) \
    X(T, 1, sequence, w::u64) \
    X(T, 2, payload, w::blob<4096>)
PORTAL_WIRE_RECORD(Packet, 0x1001, 1, PACKET_FIELDS);
```

Invoke `PORTAL_WIRE_RECORD` at global scope, outside the user namespace. The
member pointer checks the mapped C++ member type. Field IDs must be nonzero,
unique and increasing. The record's type ID and revision are explicit protocol
identifiers. No C++ padding, object representation, RTTI, compiler type name,
source line number, raw pointer or runtime field table is serialized.

A single replayable method table can project aliases, registration helpers or
documentation. `tests/typed_rpc/methods.def` is an example:

```cpp
// methods.def; no include guard:
PORTAL_TYPED_METHOD(Add,  0x101, 1, w::i32, w::i32, w::i32)
PORTAL_TYPED_METHOD(Echo, 0x104, 1, w::record<Packet>, w::record<Packet>)

// In a declaration projection:
#define PORTAL_TYPED_METHOD(Name, Id, Revision, ...) \
    using Name = rpc::method<Id, Revision, __VA_ARGS__>;
#include "methods.def"
#undef PORTAL_TYPED_METHOD
```

The first codec is the return type; the remaining codecs describe the arguments.
`Add::signature_t` is `int32_t(int32_t,int32_t)`. The same descriptor generates
request/response encoders, checked decoding and the native server binding.
`wire::none` projects a void native return and a `wire::unit` checked result.
Method descriptors expose `id`, `revision` and `arity`; codecs expose `min_size`.
These constants support value and reference use in C++14, including forwarding
APIs such as `vector::emplace_back`, across translation units.

**The existing Portal `.def`/backend lowering mechanism is not replaced or
transparently intercepted.** Typed RPC exposes an explicit
method descriptor; its `signature_t` is a projection, not a completed general
`backend_base` contract-aware invocation hook. Legacy first-viable overloads
remain legacy semantics. Typed methods select one explicit signature before
encoding; use distinct IDs/revisions when exposing distinct wire contracts.

## Synchronous owning calls

```cpp
rpc::router server;
if (server.define<Add>([](std::int32_t a, std::int32_t b) noexcept {
        return std::int32_t(a + b); // application must define domain overflow rules
    }) != w::errc::ok) {
    // Registration failed, e.g. a duplicate ID/revision.
}
rpc::loopback_transport transport(server);
rpc::workspace storage(64 * 1024); // retention budget per cached byte buffer

auto answer = rpc::call<Add>(transport, storage, w::limits{},
                             std::int32_t(20), std::int32_t(22));
if (answer) {
    // answer.value == 42
} else {
    // answer.error is a typed protocol error code.
}
```

A synchronous transport implements
`void round_trip_into(const bytes& request, bytes& response)`. It must finish
all access before returning and must replace the complete response, not append
to or retain a reference to the previous buffer. Transport exceptions propagate.
`workspace` is confined to one worker, not internally synchronized. Every
active/reentrant call takes separate buffers. The caller must keep the workspace
alive until every active scope returns. Registration finishes before concurrent
router use; native bound functions must support the selected concurrency.

Cached buffers retain initialized byte elements, not just capacity. A successful
encoder resizes to the exact new frame size and overwrites **every** published
byte. This avoids clearing/zero-filling the same large buffer on every call.
There is no implicit TLS/global pool. Idle retained capacity is bounded per
buffer; active request size, recursion, total in-flight memory and allocator
retention are not bounded by this cache setting. Contents are not securely
wiped. `release_cached_storage()` releases idle allocations; active scopes can
repopulate the caches on return. Cache budgets must include wire headers, not
just application payloads.

Results own strings/vectors/records. A response can be destroyed or its storage
reused after decoding. The server permits request and reply to be the same vector
because it completes owning decode before overwriting the reply. General codec
output must not alias storage referenced by its input values. Low-level reader
and writer helpers require valid backing storage and are not owning containers.

## Supported codecs and validation

| Codec | C++ projection | Wire shape |
|---|---|---|
| `u8/u16/u32/u64`, `i8/i16/i32/i64` | fixed-width integers | fixed width, LE; signed two's-complement wire bits |
| `boolean` | `bool` | one byte, only 0 and 1 accepted |
| `f64` | IEEE-754 binary64 `double` | eight LE bytes, preserves bit representation |
| `blob<N>` | owning `std::string` | u32 byte count then raw bytes, at most N |
| `array<C,N>` | `std::array<C::value_type,N>` | N consecutive values, no dynamic count |
| `sequence<C,N>` | owning `std::vector` | u32 element count then values, at most N |
| `maybe<C>` | `wire::option<T>` | strict boolean then optional value |
| `record<T>` | existing T | declared members in increasing field-ID order |
| `none` | `unit` / void native return | empty body |

`blob` is binary, not UTF-8 text. UTF-8 policy/normalization is not implemented.
`vector<bool>` is rejected. No object graphs, cycles, raw pointers, remote
capabilities or callback handles are implicitly serialized. Their identity,
authority, retirement and revocation rules need a separate marshalling contract.
Supported native value types must be default constructible and movable/assignable;
the async adapter additionally inherits Portal's nothrow move requirements.
Record constructors and custom codec extensions are trusted application code;
structural validation gates the registered **business handler**, not every
possible user constructor. Custom `size` implementations must report failure
through `measure::fail`, and `size`/`write` must meet their nonthrowing contract.

A public decode stages into a local owning value and commits only after the
whole body has been consumed. The native handler is never called on failed
structural decoding. Reader budgets check the frame size, cumulative requested
variable-storage bytes, cumulative element count and nesting depth. The default
limits are 1 MiB frames, 4 MiB logical decoded storage, 65,536 elements and depth
32. Decoded storage accounting is a logical allocation-request budget, not an RSS
or stack-space guarantee: allocator overhead/capacity, statically sized objects,
user constructors and handler work are outside it. Very large static schema
layouts are a developer responsibility. Encode checks field/byte-size limits;
receive-side depth/elements/storage budgets remain independent policy checks.

Truncation, extra trailing data, invalid boolean, length overflow, oversized
fields, excessive counts/depth and incompatible headers are errors. Outgoing
arguments must exactly match declared value types after decay; implicit wire
narrowing is rejected at compile time. A native return must match the declared
return type exactly. Registering the same method ID/revision twice is rejected.

Allocation failure can throw; no-exception builds retain their allocator's
termination policy, not guaranteed recovery. Handler exceptions become a fixed
wire code without exporting their text. An exception during result encoding
occurs **after** the handler may have produced side effects: there is no rollback,
transactionality, retry safety or exactly-once business execution guarantee.

## Protocol v1 and compatibility

The public contract covers the documented entry points, owning result semantics,
strict validation and protocol-v1 bytes. API migrations are documented explicitly;
adding a new RPC style does not alter existing generated import/export behavior.

Protocol v1 fixes the envelope layout, codec encodings and signature-fingerprint
algorithm. Its requests, successful responses, error responses, records and
fingerprints are checked against independent Python golden bytes. Incompatible
changes to these shared rules require a new protocol version. Unknown protocol
versions are rejected.

Assign method and record IDs deliberately and keep them stable. An incompatible
change to a method signature, record layout, field meaning or codec bounds
requires a new revision. Increase a changed record's revision and the revision
of each affected method, and retain older method descriptors/handlers while
older peers are supported. Field IDs are permanent logical identifiers; they do
not enable automatic field skipping or optional-field evolution in this format.

Both requests and replies start with this 32-byte header (all integers LE):

| Offset | Width | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `PTR1` request / `PTS1` response |
| 4 | 2 | protocol version, 1 |
| 6 | 2 | status; request must be zero |
| 8 | 8 | explicit method ID |
| 16 | 4 | explicit method revision |
| 20 | 4 | exact body byte length |
| 24 | 8 | deterministic signature fingerprint |

Replies with nonzero status have no body. Status codes are defined by
`wire::errc`: ok, malformed, limit_exceeded, invalid_value, unknown_method,
incompatible_schema, handler_failed, transport_failed (0 through 7).

The fingerprint mixes stable codec tags, bounds, member IDs, record IDs/revisions
and the method argument/return shape. It does not hash C++ source strings or
object memory. This is a 64-bit non-cryptographic mismatch detector, **not**
authorization, authentication, a MAC or collision-free proof. Schema IDs,
revisions and field meanings still need deliberate deployment governance.
No automatic schema evolution, negotiation or arbitrary field skipping is
promised. Changed schema/bounds require a corresponding revision/migration
policy. Different names with the same wire declaration are not detected as
semantic changes. Fields carry no per-value type tags at runtime.

The header is intentionally explicit, not universally smaller than legacy RPC:
a two-i64 request is 48 bytes (legacy 35); its i64 reply is 40 (legacy 15).
The two-i32 body is only 8 bytes (legacy tagged integer body 18), but its complete
request is still 40 versus 35. Smaller bodies do not imply smaller small packets.
No request correlation ID, transport stream framing, retry, ordering, encryption,
connection management or multiplexing is supplied. A transport/session must
match each response to the correct operation before invoking its completion.

## Async ownership and the FF seam

```cpp
#include <portal/rpc/typed_async.h>

auto call = rpc::make_rpc_invocation<Add>(shared_transport, w::limits{},
                                        std::int32_t(20), std::int32_t(22));
std::move(call).start([](portal::async_result<std::int32_t> result) noexcept {
    // Observe result.value or result.error.
});
```

`Transport::async_round_trip(bytes request, Callback)` returns
`portal::async_submission`. The transport takes ownership of the request vector.
On acceptance it must make exactly one terminal callback with an owning response
and `async_error` (and may do so inline). On rejection or throw-before-acceptance
it must retain no callback and never call it. It must not throw after accepting
or after calling back. Request buffers cannot be lent from a worker-local
synchronous workspace. The adapter decodes an owning result before delivery.
`make_rpc_invocation` itself can throw while preparing request storage, before
`start()` is reached. A field/size preparation error is delivered as rejection
when started.

This uses the **existing** concrete `static_invocation` state, including its
local completion-token pin and delivery/retirement rules. It introduces no
additional shared operation state, `std::function`, blueprint or handler slot.
It does not fuse away Portal's state allocation or FF's awaitable storage. It is
a terminal-only transport contract, not an added early-cancellation interface.
Do not equate logical timeout/cancellation with physical buffer retirement.

The returned concrete Call can be input to a long-lived FF blueprint using the
existing `portal::ff::await_static_invocation<Call>()`, followed by a fresh
`make_fast_runner_view` per request. This avoids rebuilding the convenience
`start_invocation` blueprint/slot for every request. See
`tests/typed_rpc/ff_test.cpp` for a complete deferred-completion/rejection gate.
The test uses actual FF headers and covers deferred completion and rejection.
CI requires this integration test using its pinned FF checkout.

## Build and validation gates

Core/actual Portal async tests:

```sh
cmake -S tests/typed_rpc -B build-typed -DCMAKE_BUILD_TYPE=Release
cmake --build build-typed --parallel 2
ctest --test-dir build-typed --output-on-failure
```

Require real FF instead of silently omitting the integration gate:

```sh
cmake -S tests/typed_rpc -B build-typed-ff -DCMAKE_BUILD_TYPE=Release \
  -DFLUX_FOUNDRY_INCLUDE_DIR=/path/to/flux_foundry \
  -DPORTAL_TYPED_RPC_REQUIRE_FF=ON
cmake --build build-typed-ff --parallel 2
ctest --test-dir build-typed-ff --output-on-failure
```

The root `PORTAL_BUILD_TESTS=ON` build includes these tests automatically. They
also support the standalone commands above. Configuration first compiles a valid
C++14 contract, then requires a specific diagnostic for each rejected schema,
argument type and return type. CMake supplies the configured compiler environment,
including MSVC. A missing standard header cannot count as a contract rejection.

Runtime coverage includes ordinary and strict/relaxed no-exception builds,
independent Python golden bytes, malformed inputs, reentrancy, threads, allocation
failure and actual FF integration. Python is optional for local native-only tests;
CI supplies it and runs the golden test. The installed-package consumer runs both
RPC styles using public headers. Root CI builds Debug/Release on Linux, macOS and
Windows, and its Valgrind job covers the native typed tests as well.

The retained performance report describes the earlier prototype's sandbox,
commands and limitations; it is not a current cross-platform performance promise.
The benchmark explicitly labels its isolated legacy codec control. See
[`benchmarks/typed_rpc/README.md`](../benchmarks/typed_rpc/README.md).
