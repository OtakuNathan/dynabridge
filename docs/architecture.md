# Architecture

Dyna Bridge models a boundary call as the product of three independent pieces:

```text
callable<Receiver, Signature> x Backend x ConverterSet
```

`callable` is the static contract. `Backend` defines runtime operations and
concrete context state. `ConverterSet` admits value types one specialization at
a time. No runtime map is needed to recover C++ callable shape because the call
site already carries it.

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
maintained registration strings and signatures from drifting apart.

C++ reflection could provide another notation for the schema. It would not by
itself choose the public whitelist, import/export direction, converter policy,
or all runtime registration metadata, so it does not eliminate the underlying
single-source requirement.

## Extension Boundary

A new backend derives from `backend_base<Derived>` and fills the operations its
runtime supports. Context types choose concrete state, such as a direct
callable, a thread-safe callable, a module, or an export registry. The core does
not select threading, scheduling, ownership, or error policy for the caller.

Converters should remain strict on export so overload probing is deterministic.
Container converters are responsible for validating every element. Platform
object identity belongs in `object_t`, not in broad `converter<T>`
specializations.
