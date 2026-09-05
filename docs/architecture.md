# Architecture

Dyna Bridge models a boundary call as the product of three independent pieces:

```text
callable<Receiver, Signature> x Backend x ConverterSet
```

`callable` is the static contract. `Backend` defines runtime operations and
concrete context state. `ConverterSet` admits value types one specialization at
a time. No runtime map is needed to recover C++ callable shape because the call
site already carries it.

Compile-time contract lookup does not remove runtime-language symbol
resolution. A backend may resolve an import name to a dynamic handle or register
an export under its generated name. It can cache an imported handle for later
calls, while a context constructed from an existing handle can skip name lookup
entirely.

## Three Channels

Values, objects, and callables cross through separate channels:

- Values use `converter<T>::to` and strict `converter<T>::from`, where `from`
  returns `optional<T>` so overload probing does not require exceptions.
- Objects use backend `object_t<Class, Direction>` handles. Imported objects
  become generated C++ delegates; exported objects unwrap generated proxies to
  native receivers.
- Callables reuse ordinary callable groups. A callback is a callable whose
  runtime handle was supplied as an argument rather than found by name.

This separation prevents a generic dynamic handle from silently masquerading
as a typed C++ object.

## One Schema, Multiple Projections

Import and export `.def` files are replayable schema tables. Each inclusion
mode derives one projection: symbol tags and names, overload type lists,
delegates, export proxies, registration functions, or compile-time checks.
Keeping these projections generated from one table prevents independently
maintained registration strings and signatures from drifting apart. Method
metadata uses `.def` source-line IDs so repeated expansion remains identical
across translation units; each method group must occupy a distinct source line.

## Static Capability Composition

`BEGIN_INTERFACE` declares a reusable, stateless member capability.
`IMPLEMENTS(name)` publicly composes its generated CRTP mixin into a concrete
import delegate or export proxy. The concrete class remains the only state
owner: import classes hold one context/object pair and export proxies hold one
native storage object. Export registration flattens interface methods onto the
concrete runtime class, so backends need no inheritance protocol.

Interfaces have no object identity, constructors, standalone registration, or
backend handles. They are flat in the current contract and cannot implement
other interfaces. Duplicate interface declarations and duplicate member names
across interfaces and the concrete class are compile-time errors. Shared import
interface metadata is receiver-neutral. Projecting that interface into a
concrete import class generates member symbols bound to the class receiver, so
the complete static call key still includes receiver identity.

C++ reflection could provide another notation for the schema. It would not by
itself choose the public whitelist, import/export direction, converter policy,
or all runtime registration metadata, so it does not eliminate the underlying
single-source requirement.

## Extension Boundary

A new backend derives from `backend_base<Derived>` and fills the operations its
runtime supports. Context types choose concrete state, such as a direct
callable, a module, or an export registry. A backend supplies runtime primitives;
it does not need to become an orchestration framework. Thread affinity and async
continuations compose through Flux Foundry runners plus an executor for the
target loop, while ownership and platform error rules remain explicit context or
backend contracts. The RPC/GLib benchmark demonstrates this split with an
external awaitable and `gsource_executor`; Node/libuv and Python interpreter-loop
executors have the same shape. Flux Foundry's executor contract is intentionally
duck typed: an extension only needs `dispatch(task&&)`. No executor base class or
core scheduling policy is required.

Import argument lowering can optionally use a backend's
`own_import_value_impl(ctx, value)` hook. It must adopt the lowered value without
throwing and return an RAII guard that the backend invocation accepts. This
protects already converted arguments if a later conversion throws, including
object and callback arguments and member receivers. Without the hook, lowered
values are forwarded by value as before. Python adopts each new reference into
`object_ref`; vectorcall borrows from those guards, while tuple calls transfer
their references to the tuple. Direct converter calls retain their existing
ownership contract.

Converters should remain strict on export so overload probing is deterministic.
Export overloads are visited in `.def` declaration order after arity filtering;
the first candidate whose argument conversions succeed wins. Declaration order
within an overload group is therefore an explicit dispatch policy. The relative
placement of constructors and ordinary member groups has no dispatch meaning.
The core does not assign C++-style conversion scores.
Container converters are responsible for validating every element. Platform
object identity belongs in `object_t`, not in broad `converter<T>`
specializations.
