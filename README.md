# Dyna Bridge

[![CI](https://github.com/OtakuNathan/dynabridge/actions/workflows/ci.yml/badge.svg)](https://github.com/OtakuNathan/dynabridge/actions/workflows/ci.yml)

Dyna Bridge is a C++14, header-only contract layer for crossing runtime
boundaries with ordinary C++ call syntax. It keeps callable shape, overloads,
class identity, and conversion requirements in the type system while a backend
supplies the platform-specific runtime operations.

```text
C++ callable shape x backend runtime x converter set = usable language bridge
```

The repository includes minimal CPython C API and Node-API backends, a framed
RPC backend, and a fake backend used by the core tests.

## Why It Exists

Dyna Bridge separates three concerns that binding libraries often combine:

- The contract declares which functions, overloads, classes, constructors, and
  member functions exist.
- A backend owns runtime handles, lookup, registration, calls, and object
  lifetime policy.
- `converter<T>` specializations move value types across the boundary.

The contract is whitelist-first. An undeclared symbol cannot be discovered
through the generated API, and a type without a converter cannot enter the
value channel. Object and callback identity use dedicated typed channels rather
than pretending every runtime handle is a value conversion.

```text
Key is static. Value is dynamic. Contract lookup is compile-time.
```

This refers to C++ contract and signature selection. A backend may still resolve
an import name to a runtime handle or register an export under its generated
name. Import lookup can normally happen once when the context is created and the
handle can then be cached; callers that already supply a handle can skip it.

## Quick Start

With Python and Node.js development headers installed, build all tests and the
two runtime examples:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DDYNABRIDGE_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The example modules expose the same C++ `add` function and `counter` class to
Python and JavaScript. Their shared contract is
[`examples/common/export.def`](examples/common/export.def); the backend-specific
entry points are
[`examples/python/module.cpp`](examples/python/module.cpp) and
[`examples/napi/addon.cpp`](examples/napi/addon.cpp).

## Declare a Contract

Select module-local declaration tables before including the bridge:

```cpp
#define DYNABRIDGE_IMPORT_DEF "my_module/import.def"
#define DYNABRIDGE_EXPORT_DEF "my_module/export.def"
#include <dynabridge/bridge.h>
```

An export table can declare a free function and a native class:

```cpp
BEGIN_CALLABLE_GROUP(add)
    DECL_CALLABLE(int, int, unsigned)
END_CALLABLE_GROUP

BEGIN_INTERFACE(addable)
    BEGIN_MEMBER_CALLABLE_GROUP(add)
        DECL_MEMBER_FUNCTION(int, int)
    END_MEMBER_CALLABLE_GROUP
END_INTERFACE

BEGIN_CLASS(native, counter)
    IMPLEMENTS(addable)
    DECL_CONSTRUCTOR(unsigned)
END_CLASS
```

Registration uses generated, typed entry points:

```cpp
dynabridge::export_add(ctx, module, native::add);
dynabridge::exports::counter::register_all(ctx, module);
```

Import declarations generate normal C++ calls and receiver delegates:

```cpp
int result = dynabridge::call_add(ctx, 12, 13u);

auto counter = dynabridge::construct<dynabridge::counter>(ctx, 13u);
int value = counter.add(29);
```

Interfaces are stateless capability sets shared by concrete bridge classes.
Generated classes publicly compose their declared interfaces while retaining a
single context/object or native-storage state. Export registration flattens
interface methods onto the concrete runtime class; no runtime inheritance or
interface object is created. Interface and concrete member names must be unique
within a class, and an interface may not implement another interface.

Free functions, function objects, and lambdas can fill declared export slots.
Export overloads use ordered first-viable dispatch: the core checks arity and
then probes strict `optional<T>` conversions in `.def` declaration order. The
first successful candidate wins, so declaration order is part of the public
dispatch policy:

```cpp
dynabridge::export_calc(ctx, module)
    .bind<int(int)>(scale_by_ten)
    .bind<int(int, unsigned)>(multiply)
    .commit();
```

## X-Macro Schema

The `.def` file is not merely a workaround for missing reflection. It is the
single, replayable bridge schema. The core interprets it multiple times to
generate symbol types, names, overload type lists, class proxies, registration
code, and compile-time probes. Reflection could change how that schema is
written, but it would not remove the need for one contract to produce several
consistent projections.

Different modules select different import and export tables. The defaults are
empty, so including `bridge.h` does not expose repository test declarations.

## Use With CMake

As a subdirectory:

```cmake
add_subdirectory(external/dynabridge)
target_link_libraries(my_addon PRIVATE dynabridge::dynabridge)
target_include_directories(my_addon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```

The final include directory makes module-local `.def` paths such as
`"my_module/export.def"` visible during schema expansion.

Tests, examples, and benchmarks default to off when Dyna Bridge is a
subproject. For an installed package:

```sh
cmake -S . -B build -DDYNABRIDGE_BUILD_TESTS=OFF
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(dynabridge CONFIG REQUIRED)
target_link_libraries(my_addon PRIVATE dynabridge::dynabridge)
```

## Scope and Status

| Area | Included status |
| --- | --- |
| Core | C++14 header-only import/export contracts, overloads, classes, callbacks, and typed object channels |
| Python | Minimal CPython C API backend with real runtime tests |
| JavaScript | Minimal Node-API backend with real Node.js runtime tests |
| RPC | Transport-agnostic framed demo backend |
| Scheduling | Optional Flux Foundry adapters for Python interpreter and libuv loop dispatch |
| Value types | Built-in Python/Node converters for `int` and `unsigned`; applications add only the types they use |
| Platforms | CI builds Debug and Release on Linux, macOS, and Windows |

This is a bridge core, not a drop-in replacement for the complete policies and
type catalogs of pybind11, nanobind, or node-addon-api. Thread attachment, the
Python GIL, Node handle scopes, scheduler choice, and platform object ownership
are not dynabridge core policy. Backends expose the required platform primitives;
Flux Foundry runners and runtime-specific executors can compose thread affinity,
asynchronous continuation, and cancellation without reimplementing orchestration
inside every backend. Contract changes require recompilation; there is
intentionally no runtime C++ symbol registry. Runtime-language symbol resolution
still belongs to the backend, and export overloads use ordered first-viable
selection rather than C++-style conversion ranking. Descriptor returns are not
currently part of the common protocol, and the RPC backend is a focused demo,
not a production transport stack.

Published timings are microbenchmarks, not whole-application forecasts. See
the [reference and benchmark notes](docs/reference.md#benchmarks) for scenarios,
environments, and comparison caveats.

## Documentation

- [Architecture and extension boundaries](docs/architecture.md)
- [Full API reference and backend walkthrough](docs/reference.md)
- [Python example](examples/python/module.cpp)
- [Node-API example](examples/napi/addon.cpp)

## License

Apache License 2.0. See [LICENSE](LICENSE).
