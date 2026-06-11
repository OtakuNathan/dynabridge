# Dyna Bridge

[![CI](https://github.com/OtakuNathan/dynabridge/actions/workflows/ci.yml/badge.svg)](https://github.com/OtakuNathan/dynabridge/actions/workflows/ci.yml)

Dyna Bridge is a small C++ bridge core for calling across C++ and dynamic
languages while keeping the C++ side simple, typed, and familiar.

The idea is to describe the contract once with normal C++ function signatures,
then let a backend decide how those calls cross a language boundary. The same
bridge layer can target JavaScript through N-API, Python through the Python C
API, Lua, or a test-only fake backend. The contract stays the same; only the
backend and converters change.

> Current namespace: `dynabridge`.

## Core Idea

Dyna Bridge treats cross-language calls as a static contract over dynamic
runtime values:

```text
Key is static. Value is dynamic. Lookup is compile-time.
```

The practical rule is:

```text
C++ callable shape × backend runtime × converter set = usable language bridge
```

The key is the declared `callable<Receiver, Signature>`. The value is whatever
runtime handle the backend owns, such as `PyObject*`, `napi_value`, or a Lua
stack value. Lookup happens through C++ overload resolution, template
specialization, and `converter<T>` selection instead of a runtime C++ registry.

Each backend plugs its runtime into the same rule instead of inventing another
binding framework.

Dyna Bridge is whitelist-first. A callable, class, overload, or conversion does
not exist at the bridge boundary unless it is declared explicitly. This is close
to an executable import/export table: no declaration means no symbol, no
converter means no boundary crossing, and no backend policy means no runtime
effect. If another runtime should not touch something, do not put it in the
bridge table; the core will not generate a path to discover or call it.

Dyna Bridge separates the problem into five pieces:

- `callable<Receiver, R(Args...)>` describes the C++ call contract.
- `free_callable<R(Args...)>` is `callable<no_receiver_t, R(Args...)>`.
- `backend_t` owns the language-specific call mechanics.
- `backend_t::converter<T>` owns value conversion with `to` and optional
  `from` probes. Core code calls them through `to_cast<T>`, `from_optional<T>`,
  and `from_cast<T>`.
- `object_t<Class, Direction>` owns object identity, handles, and native
  binding.

Forward call, C++ to dynamic language:

```text
C++ receiver/args -> to_cast<T> -> backend call -> from_cast<R> -> C++
```

Export call, dynamic language to C++:

```text
dynamic value args -> converter<T>::from optional probe -> C++ callable -> to_cast<R>
dynamic self handle -> backend object_t -> generated export proxy -> native receiver
```

This keeps runtime details such as `napi_value`, `PyObject*`, or Lua stack slots
outside the bridge core.

Value conversion and object binding are intentionally separate. `converter<T>`
is the value channel for integers, strings, containers, and return values.
Class identity flows through `object_t`: export member calls bind the dynamic
`self` handle to an export object and unwrap the generated C++ proxy from that
object before invoking the method. The proxy owns or borrows the native C++
receiver and forwards only whitelisted member functions.

`to_cast<T>(ctx, value)` forwards to `converter<T>::to`.
`from_optional<T>(ctx, dynamic)` forwards to `converter<T>::from`, which returns
`optional<T>`. Empty means the dynamic value is not accepted by that C++ target
type. `from_cast<T>(ctx, dynamic)` is the must-succeed convenience path: it
unwraps the optional result and reports a conversion error when failure is
final.

The headers follow the same split. `callable.h` defines only the contract and
callable groups. `import_callable.h` interprets a contract as C++ calling into a
dynamic runtime. `export_callable.h` interprets the same contract as a
dynamic-runtime callback into C++. `import.h` and `export.h` are the X-macro
entry points that generate public sugar from the selected import/export def
files. The core defaults are empty, so real modules opt into their own tables.

## Included Backends

The repository includes three backend implementations:

- `tests/fake_backend.h` is a small in-memory backend for core tests.
- `dynabridge/backends/python.h` is a minimal Python C API backend.
- `dynabridge/backends/napi.h` is a minimal N-API backend.

Backend headers declare their `converter<T>` primary template and include their
default converter specializations at the end, for example
`python.h` includes `python_converters.h`. Project-specific import proxy
converters, such as `converter<counter<Context>>`, stay near the integration
that owns the imported object contract. Exported classes do not use value
converters; the backend binds a generated proxy object to the dynamic class
instance.

Code that uses the Python C API should include
`dynabridge/backends/python_api.h` instead of including `Python.h` directly.
The wrapper keeps MSVC Debug builds compatible with a normal release Python
runtime; define `DYNABRIDGE_PYTHON_KEEP_MSVC_DEBUG` only when linking against a
real Python debug build.

## Callable Declarations

Import and export declarations are separate X-macro files. The repository tests
use `tests/import.def` and `tests/export.def` as examples. An import def
generates C++ calls into the other runtime:

```cpp
BEGIN_CALLABLE_GROUP(foo)
    DECL_CALLABLE(void, int, int)
    DECL_CALLABLE(void, int)
    DECL_FUNCTION(void (unsigned))
END_CALLABLE_GROUP

BEGIN_CALLABLE_GROUP(calc)
    DECL_CALLABLE(int, int, unsigned)
END_CALLABLE_GROUP

BEGIN_CLASS(counter)
    DECL_CONSTRUCTOR(unsigned)

    BEGIN_MEMBER_CALLABLE_GROUP(add)
        DECL_MEMBER_FUNCTION(int, int)
    END_MEMBER_CALLABLE_GROUP

    BEGIN_MEMBER_CALLABLE_GROUP(value)
        DECL_MEMBER_FUNCTION(int)
    END_MEMBER_CALLABLE_GROUP
END_CLASS
```

An export def generates bindings exposed from C++ to the other runtime.
`BEGIN_CLASS(ns, clazz)` exports dynamic class `"clazz"` and binds it to native
C++ type `ns::clazz`:

```cpp
BEGIN_CALLABLE_GROUP(calc)
    DECL_CALLABLE(int, int)
    DECL_CALLABLE(int, int, unsigned)
END_CALLABLE_GROUP

BEGIN_CLASS(native, counter)
    DECL_CONSTRUCTOR(unsigned)

    BEGIN_MEMBER_CALLABLE_GROUP(add)
        DECL_MEMBER_FUNCTION(int, int)
    END_MEMBER_CALLABLE_GROUP

    BEGIN_MEMBER_CALLABLE_GROUP(value)
        DECL_MEMBER_FUNCTION(int)
    END_MEMBER_CALLABLE_GROUP
END_CLASS
```

`DECL_CALLABLE(R, Args...)` is the core free/global declaration.
`DECL_FUNCTION(sig)` is independent sugar for legacy C++ function-signature
style. Inside class declarations, `DECL_CONSTRUCTOR(Args...)` whitelists
constructor calls: import declarations allow C++ to construct foreign objects,
while export declarations allow the foreign runtime to construct C++ native
objects. `DECL_MEMBER_FUNCTION(R, Args...)` declares member overloads for import
and export class declarations.
Keeping import and export def files separate means importing a callable does
not automatically generate an export API for it.

The default declaration files are empty. Select module-specific declarations
before including the bridge:

```cpp
#define DYNABRIDGE_IMPORT_DEF "my_module_import.def"
#define DYNABRIDGE_EXPORT_DEF "my_module_export.def"
#include "dynabridge/bridge.h"
```

This lets each module or domain own its import/export table while keeping the
same core headers. The core headers are guarded, but the X-macro generation
wrappers are intentionally repeatable, so one translation unit can include
`bridge.h` multiple times with different def macros. Generated C++ names still
share the selected namespace, so use distinct group and class names when
multiple domains are expanded together.

Callable arguments may be values, `const T&`, or `T&&`. Non-const lvalue
references such as `T&` are rejected because a bridge boundary cannot provide a
meaningful writable C++ reference into another runtime.

Each free group becomes an overload set, a same-name binder, and a direct import
function with a `call_` prefix:

```cpp
dynabridge::fake_backend::import_context_t<recorded_call> ctx(recorded_call{});

dynabridge::call_foo(ctx, 1);
dynabridge::call_foo(ctx, 1, 2);
dynabridge::call_foo(ctx, 1u);

int result = dynabridge::call_calc(ctx, 3, 4u);

auto foo = dynabridge::foo(ctx);
foo(1);
foo(1, 2);

auto counter = dynabridge::bind_receiver<dynabridge::counter>(ctx, /* handle */);

int member_result = counter.add(29);
int current = counter.value();

auto constructed = dynabridge::construct<dynabridge::counter>(ctx, 13u);
int constructed_value = constructed.value();
```

The call looks like ordinary C++, but the backend decides what it really means.

## Importing From a Lookup Domain

The selected import def also generates static symbol metadata under
`dynabridge::import_symbols`. Use `import_from` when a backend can resolve a
declared symbol from a dynamic lookup domain such as a Python module, a
JavaScript object, or a Lua table:

```cpp
auto ctx = dynabridge::import_from<
    dynabridge::import_symbols::calc,
    dynabridge::py_backend::import_context_t>(module);

int result = dynabridge::call_calc(ctx, 3, 4u);
```

The lookup name comes from the X-macro declaration, so user code does not repeat
string keys such as `"calc"`. Backends decide what the source means. Python can
look up an attribute on a module or import by module name; N-API can look up a
named property on an exports object or another JS object. Unsupported sources
are rejected at compile time by `backend_base`.

Import symbols are type tags, not runtime enum entries. The type itself is the
static key; the string name is only metadata for backends that need a dynamic
lookup name.

Class declarations also generate nested member symbols:

```cpp
using counter_symbol = dynabridge::import_symbols::counter;
using add_symbol = dynabridge::import_symbols::counter::add;

static_assert(std::is_same<
    dynabridge::import_symbol_traits<add_symbol>::receiver_symbol_t,
    counter_symbol>::value, "add belongs to counter");
```

This keeps member functions as first-class callable symbols: the receiver is
part of the static key, not an afterthought attached to a string.

## Exporting C++ to a Dynamic Language

Export binding exposes C++ callables as dynamic-language functions.

Free functions can be exported directly; the signature is inferred from the
function pointer:

```cpp
int add(int a, unsigned b) {
    return a + static_cast<int>(b);
}

dynabridge::export_free_callable(ctx, module, "add", add);
```

When a callable group already provides the name, use the generated export helper
and avoid the runtime string:

```cpp
dynabridge::export_calc(ctx, module, add);
```

Lambda or function objects use an explicit signature:

```cpp
dynabridge::export_calc<int(int, unsigned)>(
    ctx,
    module,
    [](int a, unsigned b) {
        return a * static_cast<int>(b);
    });
```

If a group declares multiple signatures, use the generated builder. Each
`bind<Signature>` fills one declared slot and moves the callable into the final
holder; `commit()` installs one dynamic function:

```cpp
dynabridge::export_calc(ctx, module)
    .bind<int(int)>([scale](int a) {
        return a * scale;
    })
    .bind<int(int, unsigned)>([](int a, unsigned b) {
        return a * static_cast<int>(b);
    })
    .commit();
```

The backend forwards `argc` and dynamic arguments to the binder. The core walks
declared signatures in order, skips arity mismatches, uses `converter<T>::from`
for recoverable conversion misses, and returns the first successful result.
Single signature exports keep the direct fast path; overload builders use a
typed slot set, not `std::function` or a runtime registry.

Class exports use the generated proxy class itself. The dynamic receiver is
bound through the backend object channel, not through `converter<T>`.
Constructor declarations create generated proxy instances and bind them to the
dynamic `self` handle. The proxy owns or borrows the native C++ object and
forwards declared member functions:

```cpp
dynabridge::py_backend::export_context_t export_ctx;
dynabridge::exports::counter<native::counter>::register_all(export_ctx, module);
```

`register_all` registers the whitelisted constructors and member functions from
the selected export def, then stores the backend class target in the export
context. During
a member call, the backend receives the dynamic `self` handle, builds
`object_t<exports::counter<T>, export_t>`, unwraps the generated proxy, and
invokes the proxy method. That proxy forwards to `T::add`, `T::value`, or any
explicit callable you bind.

C++ can also create dynamic instances after the type is registered:

```cpp
auto object = dynabridge::make_exported<
    dynabridge::exports::counter<native::counter>>(
    export_ctx,
    native::counter{13});

dynabridge::export_instance<
    dynabridge::exports::counter<native::counter>>(
    export_ctx,
    module,
    "global_counter",
    dynabridge::borrow(existing_counter));
```

`make_exported` returns the backend object handle. `export_instance` creates
the object and stores it on the target module/table under the supplied name.
Passing a native value gives the proxy owned state; `borrow(obj)` gives it a
non-owning reference. Exported instances are usually created during module
bootstrap against the long-lived export context, so exported output stays
centralized with the rest of module registration.

The lower-level member binder also accepts ordinary C++ member function
pointers. Native member pointers are invoked through the proxy's `native()`:

```cpp
auto add = dynabridge::create_export_member_callable_binder<
    dynabridge::exports::counter<native::counter>,
    int(int)>(
    ctx,
    &native::counter::add);
```

Class delegates hold a context plus an object from the other runtime. Generated
member methods forward through the same converter barrier:

```cpp
auto counter = dynabridge::bind_receiver<dynabridge::counter>(ctx, /* handle */);

counter.add(1);
counter.value();
```

The delegate is just C++ sugar over `callable<counter, R(Args...)>`.
`bind_receiver` is a thin alias for constructing the backend `object_t` and
calling `bind`; `construct<Delegate>` constructs a backend `object_t` through
the `construct_object` tag and returns the same delegate shape. The backend and
context still decide how the foreign object is retained, released, constructed,
or destroyed.

Backend objects carry the runtime handle plus a direction:

```cpp
object_t<Receiver, import_t> // C++ projection of a foreign object
object_t<Class, export_t>    // dynamic wrapper bound to a generated proxy
```

The static type is the bridge identity; the actual handle remains a backend
detail such as `PyObject*`, `napi_ref`, or a test handle. Import classes use the
generated C++ projection, while export classes use generated
`exports::<name><Native>` proxies.

## Extending With a Backend

To add a language backend, define a backend type derived from
`backend_base<backend_t>` and provide a context plus converters.

The context decides the concrete runtime state. The backend decides the rules.
For example, one N-API context can hold a direct `napi_value` function while
another holds a `napi_threadsafe_function`; both can satisfy the same bridge
contract through backend hooks. The core does not know which policy was chosen,
so unused runtime policies do not add dispatch cost.

```cpp
struct my_backend : dynabridge::backend_base<my_backend> {
    // Optional, lets the core preflight converter<T>::from/to probes.
    using dynamic_value_t = dynamic_value;

    template <typename Callable>
    struct import_context_t {
        using backend_t = my_backend;

        // Store runtime handles and policy here, for example napi_env,
        // napi_value, napi_threadsafe_function, or PyObject*.
        Callable callable_;
    };

    struct export_context_t {
        using backend_t = my_backend;

        // Store the export domain here: class targets, prototype handles,
        // callbacks, and other registration state.
    };

    template <typename T>
    struct converter;
};
```

Then specialize converters:

```cpp
template <>
struct my_backend::converter<int> {
    template <typename Context>
    static dynamic_value to(Context& ctx, int value);

    template <typename Context>
    template <typename Context>
    static dynabridge::optional<int> from(Context& ctx, dynamic_value value);
};
```

Use `from` for recoverable value mismatches, not runtime failures such as
allocation errors or failed API calls. A mismatch returns an empty optional:

```cpp
template <>
struct my_backend::converter<int> {
    template <typename Context>
    static dynabridge::optional<int> from(Context& ctx, dynamic_value value) {
        if (!is_integer(ctx, value)) {
            return {};
        }
        return dynabridge::optional<int>(read_integer(ctx, value));
    }
};
```

`optional<T>` is dynabridge's lightweight in-place optional for converter probe
results. Empty means "this overload does not accept the dynamic value"; it is
normal control flow and should stay off the exception path. For trivial value
types such as `int` or runtime handles, `optional<T>` preserves trivial
destruction, copy, move, and assignment so converter probes remain cheap.

For a real N-API backend, `dynamic_value` would be `napi_value`. For a Python
backend, it would be `PyObject*`. The bridge API does not change.

Imports enter a backend through one low-level hook. `backend_base` converts the
C++ receiver and arguments first, then calls `invoke_impl`. Free callables use
`no_receiver_t` as their receiver tag:

```cpp
template <typename Receiver, typename R, typename... DynamicArgs>
static R invoke_impl(import_context_t& ctx, dynabridge::no_receiver_t, DynamicArgs... args);

template <typename Receiver, typename R, typename DynamicReceiver, typename... DynamicArgs>
static R invoke_impl(import_context_t& ctx, DynamicReceiver receiver, DynamicArgs... args);
```

Imported object construction enters through the backend object handle. The
constructor declaration selects an overload, then `construct<Delegate>` builds
`object_t<Receiver, import_t>(ctx, construct_object, args...)` and wraps it in the
generated delegate:

```cpp
template <typename Receiver, typename Direction>
class object_t
    : public object_base_selector<object_t<Receiver, Direction>,
          my_backend, Receiver, Direction>::type {
public:
    template <typename... Args>
    object_t(import_context_t& ctx, dynabridge::construct_object_t, Args&&... args);

    template <typename... Args>
    void construct_import_object_impl(import_context_t& ctx, Args&&... args);
};
```

Lookup-domain imports use a separate hook. `backend_base` validates that the
backend can resolve a symbol from the provided source, then forwards the
generated static name:

```cpp
template <typename Symbol, typename Context, typename Source>
static Context import_impl(Source& source, const char* name);
```

Implement this only for sources the backend wants to expose. For example, a
Python backend may support module objects and module names, while an N-API
backend may support exports objects.

The Python backend stores `PyObject*` handles with reference-counted RAII. Free
exports are attached to Python modules, while class exports create a Python type
target and attach generated member wrappers to it. The callable wrapper supports
descriptor binding, so instance calls provide the receiver automatically. Import
calls use `PyObject_Vectorcall` on CPython 3.8+ and fall back to tuple calls on
older Python versions. Exported Python callables also implement the vectorcall
protocol on CPython 3.8+, so Python-to-C++ calls can avoid tuple packing when
the interpreter uses that fast path. Exported Python constructors are installed
as `__init__`; native C++ state is stored in a hidden `PyCapsule` and destroyed
by the capsule finalizer.

`dynabridge/backends/python_api.h` is the canonical include for `Python.h`.
On MSVC Debug builds, Python's headers otherwise assume a debug Python ABI when
`_DEBUG` is defined, which does not match the release Python installed on most
CI and developer machines. The wrapper temporarily hides `_DEBUG` only while
including `Python.h`, then restores it for the rest of the translation unit.

The N-API backend's `object_t` stores `napi_value` handles through `napi_ref`.
`import_context_t` owns the imported callable through an internal `object_t`,
while delegate objects own receiver handles through their own `object_t`. The
context also caches current-scope callable and no-receiver handles for the hot
path; call `refresh()` when reusing an import context in a new N-API handle
scope. Free exports attach functions to an exports object. Class exports create
a constructor target and attach member wrappers to its prototype; the member
wrapper receives the JavaScript `this` value as the bridge receiver.

Class exports own or borrow native C++ state through a generated proxy. The
type name is declared in the selected export def through
`BEGIN_CLASS(ns, clazz)`, and the native type is selected at the export call
site:
`dynabridge::exports::clazz<ns::clazz>::register_all(export_ctx, module)`.
Constructor signatures must be whitelisted with `DECL_CONSTRUCTOR(Args...)`;
`register_all` enables them together with the declared member functions and
stores the class target in `export_ctx`. The backend constructor callback builds
`object_t<Class, export_t>(ctx, self, args...)`; the default core construction
uses `new Class(args...)`, where `Class` is the generated proxy. Python binds
the proxy with a hidden `PyCapsule`; N-API binds it with `napi_wrap`.

```cpp
namespace dynabridge {
    namespace native {
        struct counter {
            explicit counter(unsigned handle);
            int add(int value) const;
        };
    }
}

dynabridge::py_backend::export_context_t export_ctx;
dynabridge::exports::counter<native::counter>::register_all(export_ctx, module);

native::counter existing_counter{13};
auto object = dynabridge::make_exported<
    dynabridge::exports::counter<native::counter>>(
    export_ctx,
    native::counter{21});
dynabridge::export_instance<
    dynabridge::exports::counter<native::counter>>(
    export_ctx,
    module,
    "global_counter",
    dynabridge::borrow(existing_counter));
```

The core probes calls before dispatch. It always checks that
`to_cast<T>(ctx, value)` exists for import arguments and for
non-`no_receiver_t` import receivers. If the backend exposes `dynamic_value_t`,
the core also checks import return values with
`from_cast<R>(ctx, dynamic_value_t{})`.

The same dynamic value type improves export diagnostics. For exported free functions,
the core checks every declared argument with
`from_cast<T>(ctx, dynamic_value_t{})`. For exported member functions, it also
checks that the backend can bind the dynamic `self` handle into
`object_t<Class, export_t>` and recover the generated proxy. Without a backend
dynamic value type, these diagnostics fall back to concrete wrapper
instantiation and backend implementation errors.

Exports are also routed through the backend. The core creates a binder and calls
`backend_t::define(ctx, target, name, binder)`, which is checked by
`backend_base` and forwarded to `define_impl`:

```cpp
struct my_backend : dynabridge::backend_base<my_backend> {
    template <typename Binder>
    static void define_impl(export_context_t& ctx, module_t& module, const char* name, Binder binder);
};
```

Class exports first call `backend_t::define_class<Class>(ctx, parent, name)`.
The backend returns its own class target. Generated constructor exports call
`define_constructor`, and generated member exports call `define` on that class
target:

```cpp
struct my_backend : dynabridge::backend_base<my_backend> {
    template <typename Class>
    static class_target_t define_class_impl(export_context_t& ctx, module_t& module, const char* name);

    template <typename Class>
    static void store_export_class_impl(export_context_t& ctx, class_target_t target);

    template <typename Class, typename Signature>
    static void define_constructor_impl(export_context_t& ctx, class_target_t& target);

    template <typename Binder>
    static void define_impl(export_context_t& ctx, class_target_t& target, const char* name, Binder binder);

    template <typename Class>
    static object_t<Class, dynabridge::export_t> bind_export_object_impl(
        export_context_t& ctx,
        dynamic_value self);

    template <typename Class, typename... Args>
    static object_t<Class, dynabridge::export_t> make_export_object_impl(
        export_context_t& ctx,
        Args&&... args);

    template <typename Class>
    static void define_export_instance_impl(
        export_context_t& ctx,
        module_t& module,
        const char* name,
        object_t<Class, dynabridge::export_t> object);
};
```

Free exports forward dynamic arguments to the binder. Member exports forward
the dynamic receiver first, followed by dynamic arguments. The binder asks the
backend to bind that receiver handle into `object_t<Class, export_t>` and then
calls `object.native(ctx)` to recover the generated proxy. Backend targets can
be N-API exports/prototypes, Python modules/types, Lua tables/metatables, or a
test-only fake table. `make_export_object_impl` creates a dynamic instance and
binds a proxy to it; `define_export_instance_impl` stores that object on the
module/table. Exported wrappers may outlive the definition call, so the context
passed to export helpers must stay alive as long as the target runtime can call
those wrappers.

## Build and Test

Configure and build:

```sh
cmake -S . -B /tmp/dynabridge-cmake-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/dynabridge-cmake-debug
```

Run tests:

```sh
ctest --test-dir /tmp/dynabridge-cmake-debug --output-on-failure
```

The current tests cover fake, Python, and N-API paths. The stub N-API test uses
`tests/napi_stub/node_api.h`, a tiny local ABI shim, so the bridge can exercise
import/export behavior without embedding Node.js. When system `node_api.h` and
`node` are available, CMake also builds a `.node` addon and runs
`napi_runtime_smoke.js` through the real Node.js runtime.

## Benchmarks

Benchmarks are microbenchmarks for hot-path call overhead. They do not measure
module loading, class registration, reflection-like discovery, or real
application work. Build them separately in Release mode:

```sh
cmake -S . -B /tmp/dynabridge-cmake-bench \
    -DCMAKE_BUILD_TYPE=Release \
    -DDYNABRIDGE_BUILD_BENCHMARKS=ON
cmake --build /tmp/dynabridge-cmake-bench --target python_call_benchmark
cmake --build /tmp/dynabridge-cmake-bench --target node_call_benchmark_addon
```

Multi-config generators place executables under a configuration subdirectory
such as `build/Release`. Set `DYNABRIDGE_BENCH_ITERS` to control the timed loop
length.

Run the Python call benchmark:

```sh
DYNABRIDGE_BENCH_ITERS=1000000 /tmp/dynabridge-cmake-bench/python_call_benchmark
```

Run the Node.js call benchmark:

```sh
DYNABRIDGE_BENCH_ITERS=1000000 node benchmarks/node_call_benchmark.js \
    /tmp/dynabridge-cmake-bench/node_call_benchmark_addon.node
```

The benchmark compares dynabridge import/export calls with raw Python C API
tuple and vectorcall baselines. If `pybind11` or `nanobind` is available at
CMake configure time, it also includes their function-call, `cpp_function`, and
overload cases. The pybind11 high-level `operator()` path builds a tuple before
calling Python; nanobind's high-level call path uses vectorcall internally. The
benchmark also includes manual `PyObject_Vectorcall` cases through the wrapper
object pointers when CPython supports it.

Timed loops keep a checksum for each case and fail if the result is not the
expected value. This keeps the calls alive and also catches accidental benchmark
drift, such as comparing overloads that compute different results.

The Node.js benchmark compares dynabridge import/export calls with raw Node-API
and, when `node-addon-api` headers are available, node-addon-api C++ wrapper
calls. Unlike CPython vectorcall, Node-API already passes callback arguments
through `napi_callback_info`; this benchmark mostly exposes wrapper cost and
backend policy choices such as callback argument extraction and receiver lookup.
The dynabridge import case uses an `import_context_t`-owned persistent callable handle
with current-scope caches, so the hot loop does not perform per-call
`napi_get_reference_value` or `napi_get_undefined` lookups.

Reference runs from two local platforms are shown below. Compare rows within the
same platform; absolute `ns/call` values depend on CPU, compiler, runtime, and
library versions.

### Windows x64

Environment:

- Hardware/OS: Windows x64 desktop.
- Compiler: MSVC 19.50, Release build.
- Runtime: Python 3.12.13.
- Runtime: Node.js 22.14.0.
- Optional comparisons: pybind11 3.0.4 and node-addon-api.

Representative `ns/call` from `DYNABRIDGE_BENCH_ITERS=5000000`:

| Python case | ns/call |
| --- | ---: |
| dynabridge import | 64.3 |
| raw C API tuple | 85.2 |
| raw C API vectorcall | 57.8 |
| dynabridge export tuple | 53.6 |
| dynabridge export vectorcall | 31.5 |
| dynabridge overload vectorcall 2 | 52.9 |
| pybind11 function call | 115.6 |
| pybind11 manual vectorcall | 72.2 |
| pybind11 cpp_function vectorcall | 202.0 |
| pybind11 overload vectorcall 2 | 234.6 |

| Node.js case | ns/call |
| --- | ---: |
| raw N-API export | 44.6 |
| dynabridge export | 47.9 |
| node-addon-api export | 65.3 |
| raw N-API overload export 2 | 56.1 |
| dynabridge overload export 2 | 56.9 |
| node-addon-api overload export 2 | 67.6 |
| raw N-API import | 124.9 |
| dynabridge import | 173.2 |
| node-addon-api import | 177.9 |

Conclusion: on this desktop build, Dyna Bridge is much faster than pybind11 for
the typed Python export and overload paths, and faster than node-addon-api for
Node.js export wrappers. Raw N-API remains the lower bound for Node.js import
calls, while Dyna Bridge stays close to node-addon-api.

### Raspberry Pi aarch64 Linux

Environment:

- Hardware/OS: aarch64 Linux on Raspberry Pi, Debian, kernel 6.12.47.
- Compiler: GCC 14.2.0, Release build.
- Runtime: Python 3.13.5.
- Runtime: Node.js 20.19.2 with Node-API 9.
- Optional comparisons: pybind11-dev 2.13.6, nanobind 2.12.0, and
  node-addon-api 8.3.1.

Representative `ns/call` from `DYNABRIDGE_BENCH_ITERS=1000000` after the
integer converter fast paths:

| Python case | ns/call |
| --- | ---: |
| dynabridge import | 169.0 |
| raw C API tuple | 315.1 |
| raw C API vectorcall | 234.6 |
| dynabridge export tuple | 140.4 |
| dynabridge export vectorcall | 95.9 |
| dynabridge overload vectorcall 1 | 92.1 |
| dynabridge overload vectorcall 2 | 104.8 |
| dynabridge overload fallback vectorcall | 142.4 |
| pybind11 function call | 349.0 |
| pybind11 manual vectorcall | 263.4 |
| pybind11 cpp_function tuple | 555.1 |
| pybind11 overload tuple 2 | 634.0 |
| nanobind function call | 290.0 |
| nanobind manual vectorcall | 226.0 |
| nanobind cpp_function vectorcall | 119.4 |
| nanobind overload vectorcall 1 | 101.2 |
| nanobind overload vectorcall 2 | 126.4 |

| Node.js case | ns/call |
| --- | ---: |
| raw N-API export | 117.7 |
| dynabridge export | 204.8 |
| node-addon-api export | 239.3 |
| raw N-API overload export 2 | 234.6 |
| dynabridge overload export 2 | 222.8 |
| node-addon-api overload export 2 | 247.7 |
| raw N-API import | 753.2 |
| dynabridge import | 754.0 |
| node-addon-api import | 733.8 |

Conclusion: on the Raspberry Pi run, Dyna Bridge beats pybind11 and is
competitive with nanobind on the measured Python vectorcall export and overload
paths. It also stays close to, or faster than, node-addon-api on the measured
Node.js export cases. Raw runtime APIs still represent the baseline when
comparing against handwritten C API code.

Treat benchmark numbers as local measurements, not portable claims.

## License

Dyna Bridge is licensed under the Apache License 2.0. See `LICENSE`.
