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

Dyna Bridge models call shape with `callable<Receiver, R(Args...)>`;
`free_callable<R(Args...)>` is its `no_receiver_t` specialization. Arguments
cross the boundary through three separate channels:

- Plain types use `backend_t::converter<T>` for value conversion.
- `OBJECT(clazz)` uses `object_t<Class, Direction>` for typed object identity.
- `CALLABLE(group)` reuses an ordinary callable group and backend context for
  function identity. A callback is not a separate abstraction.

Forward call, C++ to dynamic language:

```text
C++ values -> to_cast<T>
imported objects -> backend dynamic handle
C++ callables -> generated export binder
backend call -> from_cast<R> -> C++
```

Export call, dynamic language to C++:

```text
dynamic values -> converter<T>::from optional probe
dynamic objects -> checked object binding -> native_t&
dynamic callables -> checked import context -> C++ callable
dynamic self handle -> backend object_t -> generated export proxy -> native receiver
```

This keeps runtime details such as `napi_value`, `PyObject*`, or Lua stack slots
outside the bridge core.

Value conversion, object identity, and callable identity are intentionally
separate. `converter<T>` is the value channel for integers, strings,
containers, and return values.
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

The repository includes four backend implementations:

- `tests/fake_backend.h` is a small in-memory backend for core tests.
- `dynabridge/backends/python.h` is a minimal Python C API backend.
- `dynabridge/backends/napi.h` is a minimal N-API backend.
- `dynabridge/backends/rpc.h` is a transport-agnostic framed RPC backend.

Backend headers declare their `converter<T>` primary template and include their
default value specializations at the end, for example `python.h` includes
`python_converters.h`. Imported and exported classes do not need converter
specializations: object identity always uses the object channel.

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

BEGIN_CALLABLE_GROUP(pass_counter)
    DECL_CALLABLE(int, OBJECT(counter), int)
END_CALLABLE_GROUP

BEGIN_CALLABLE_GROUP(pass_transform)
    DECL_CALLABLE(int, CALLABLE(transform), int)
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

BEGIN_CALLABLE_GROUP(use_callback)
    DECL_CALLABLE(int, CALLABLE(callback), int)
END_CALLABLE_GROUP

BEGIN_CALLABLE_GROUP(consume_counter)
    DECL_CALLABLE(int, OBJECT(counter), int)
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

### Object and Callable Parameters

Descriptors are interpreted by the direction of their def file:

- Import `OBJECT(counter)` accepts `const counter<Context>&` and forwards its dynamic
  handle without invoking a converter.
- Export `OBJECT(counter)` validates the dynamic class and passes the bound
  `native::counter&` to C++.
- Import `CALLABLE(transform)` accepts a free function, lambda, functor, or
  built overload set and exposes it as a temporary dynamic function.
- Export `CALLABLE(callback)` validates a dynamic callable, creates the normal
  import context, and passes that context by lvalue reference. C++ calls it with
  `call_callback(callback_ctx, args...)`.

`OBJECT` parameters are borrowed for the duration of the call. Callable context
ownership and thread policy remain backend/caller responsibilities. Descriptor
returns are intentionally unsupported; use normal value returns or an explicit
backend object API. The included RPC backend remains scalar and rejects these
descriptors until an RPC implementation supplies identity hooks.

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

The same slots can build a standalone callable argument without registering a
module symbol:

```cpp
auto transform = dynabridge::bind_transform()
    .bind<int(int)>(scale_by_ten)
    .bind<int(int, unsigned)>(multiply)
    .build();

dynabridge::call_pass_transform(ctx, std::move(transform), 6);
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
dynabridge::exports::counter::register_all(export_ctx, module);
```

`register_all` registers the whitelisted constructors and member functions from
the selected export def, then stores the backend class target in the export
context. During
a member call, the backend receives the dynamic `self` handle, builds
`object_t<exports::counter, export_t>`, unwraps the generated proxy, and
invokes the proxy method. That proxy forwards to `T::add`, `T::value`, or any
explicit callable you bind.

C++ can also create dynamic instances after the type is registered:

```cpp
auto object = dynabridge::make_exported<
    dynabridge::exports::counter>(
    export_ctx,
    native::counter{13});

dynabridge::export_instance<
    dynabridge::exports::counter>(
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
    dynabridge::exports::counter,
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
`exports::<name>` proxies. `BEGIN_CLASS(ns, clazz)` fixes the native type to
`ns::clazz`; completeness is required only when the proxy is registered or
instantiated.

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
as `__init__`; on CPython 3.8+, the class type also uses vectorcall to allocate
the instance and enter the same typed constructor holder directly. Each exported
class is a dedicated extension type whose instance layout stores the generated
proxy inline plus its context and destroy thunk; `tp_dealloc` releases the proxy
through the backend lifecycle hook.

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
`BEGIN_CLASS(ns, clazz)`, which also fixes the native type. Registration is the
non-template call
`dynabridge::exports::clazz::register_all(export_ctx, module)`.
Constructor signatures must be whitelisted with `DECL_CONSTRUCTOR(Args...)`;
`register_all` enables them together with the declared member functions and
stores the class target in `export_ctx`. The backend constructor callback builds
`object_t<Class, export_t>(ctx, self, args...)`; the default core construction
uses `new Class(args...)`, where `Class` is the generated proxy. Python binds
the proxy in its extension-instance storage; N-API binds it with `napi_wrap`.
On Node-API 8+, each exported object also receives a per-proxy `napi_type_tag`.
Object arguments and member receivers validate that tag before unwrapping the
native proxy; older Node-API headers fall back to constructor-based
`napi_instanceof` validation.

Member receiver validation is a compile-time backend policy. The default
`napi_backend::export_context_t` is checked. An embedding that guarantees the
receiver type at every call site can explicitly select
`napi_backend::trusted_export_context_t`; member calls then perform only
`napi_unwrap`, while ordinary exported-object arguments remain checked. Calling
a trusted member with a forged JavaScript `this` value violates that policy and
can cause undefined behavior.

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
dynabridge::exports::counter::register_all(export_ctx, module);

native::counter existing_counter{13};
auto object = dynabridge::make_exported<
    dynabridge::exports::counter>(
    export_ctx,
    native::counter{21});
dynabridge::export_instance<
    dynabridge::exports::counter>(
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

The same dynamic value type improves export diagnostics. Values probe
`converter<T>::from`; objects probe checked class binding; callables probe
dynamic callable import. Exported member functions additionally check that the
backend can bind dynamic `self` and recover the generated proxy.

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

    template <typename ClassTag, typename ImportObject>
    static dynamic_value to_dynamic_object_impl(
        import_context_t& ctx,
        const ImportObject& object);

    template <typename ClassTag>
    static dynabridge::optional<object_t<
        typename ClassTag::proxy_t, dynabridge::export_t>>
    try_bind_export_object_impl(export_context_t& ctx, dynamic_value value);

    template <typename ImportGroup>
    static dynabridge::optional<import_context_t>
    try_import_callable_impl(export_context_t& ctx, dynamic_value value);

    template <typename ExportGroup, typename Binder>
    static dynamic_value make_export_callable_impl(
        import_context_t& ctx,
        Binder binder);

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

## RPC Backend

RPC is the same bridge contract with a serialized `dynamic_value_t`. The RPC
backend hashes the generated static symbol name once, converts arguments to
tagged wire values, and delegates I/O to a transport with one operation:

```cpp
rpc::bytes round_trip(const rpc::bytes& request);
```

The bundled `rpc::loopback_transport` still encodes and decodes complete frames,
so it is useful for protocol tests without sockets. A TCP, shared-memory, QUIC,
or application message-bus transport can implement the same operation without
changing declarations or call sites. The current backend supports free calls,
strict overload dispatch, `void`, integer, floating-point, boolean, and string
values. Object identity and remote class lifetime policy remain transport/domain
work rather than part of this minimal backend.

The synchronous import path preserves argument reference categories until
encoding: `const std::string&` is encoded through a non-owning view, while
`std::string&&` transfers ownership into the wire value. Decoded values are
moved into single-signature exports and return conversion; overload probing
keeps a const view so a failed candidate cannot consume arguments needed by the
next candidate.

```cpp
dynabridge::rpc_backend::export_context_t export_ctx;
dynabridge::rpc::router server;
dynabridge::export_rpc_add(export_ctx, server, &add);

dynabridge::rpc::loopback_transport transport(server);
using context_t = dynabridge::rpc_backend::import_context_t<
    dynabridge::rpc::loopback_transport>;
auto ctx = dynabridge::import_from<
    dynabridge::import_symbols::rpc_add, context_t>(transport);

int result = dynabridge::call_rpc_add(ctx, 3, 4u);
```

See `tests/rpc_import.def`, `tests/rpc_export.def`, and
`tests/rpc_backend_test.cpp` for the complete declaration and binding example.

## Dynabridge + Flux Foundry

The RPC backend can remain a small typed wire layer while
[Flux Foundry](https://github.com/OtakuNathan/flux_foundry) supplies orchestration. This is a
composition of existing responsibilities rather than a second RPC framework:

```text
dynabridge callable contract
    -> Flux Foundry encode flow
    -> external async transport awaitable
    -> Flux Foundry decode/error flow
    -> typed dynabridge result
```

The RPC benchmark includes a concrete adapter with one external operation named
`web_io`. That operation owns one encoded request, performs one complete
request/response exchange, and returns the response frame. It deliberately does
not split a single I/O behavior into separate send and receive awaitables:

```cpp
auto call = flux_foundry::make_blueprint<rpc_request, rpc_error>()
    | flux_foundry::then(encode_request)
    | flux_foundry::await_external_async<web_io>()
    | flux_foundry::then(decode_response)
    | flux_foundry::end();
```

The benchmark implementation uses `then` around the throwing codec operations
so protocol and conversion failures become `result_t` errors. A blocking
`web_io` adapter isolates flow-composition cost. The production-shaped Linux
path uses a nonblocking `GSocket`, a persistent read `GSource`, and a temporary
write source only when the socket would block. Its completion callback resumes
decode and delivery inline on the owning `GMainContext` thread. Flux Foundry's
`gsource_executor` supplies the MPSC plus `eventfd` ingress for work arriving
from other threads; calls already in the GLib domain do not pay a redundant
executor hop. No separate asynchronous RPC facade or user callback is needed
because the entire call is already a blueprint and RPC I/O is its awaitable
boundary.

This composition matters because a Flux Foundry blueprint is a typed graph,
not a runtime list of callbacks. Adjacent synchronous calculation nodes are
zipped into fused callables, consecutive scheduling nodes are reduced, and
policies live in `flat_storage` with empty-base optimization. Encode and
validation can therefore fuse before the I/O boundary; decode and result
mapping can fuse after it. Only the real asynchronous transport operation needs
an awaitable continuation.

The existing flow primitives cover most heavier RPC semantics:

| RPC semantic | Flux Foundry mapping |
| --- | --- |
| Transport submission/completion | `external_async_awaitable` with `init_ctx`, `submit`, `collect`, and `destroy_ctx` |
| Typed success and failure | `result_t<T, E>`, `then`, `on_error`, and `catch_exception` |
| Resume scheduling | `via` or the executor supplied to `await` |
| Cancellation | `flow_runner` plus `awaitable_base` |
| Lowest-overhead calls | `flow_fast_runner` plus `fast_awaitable_base` |
| Parallel replicas or shards | `await_when_all` / `await_when_any` and their fast variants |
| Hedged request or timeout race | network and timer subflows combined with `when_any` |
| Backpressure | the selected executor and its bounded queue policy |
| Retry and recovery | an explicit recovery subflow, preserving retry state in its typed context |

This also preserves pay-for-use behavior. A call that does not request
cancellation can use the fast lane. A call without hedging has no aggregator.
Authentication, tracing, version checks, compression, and schema adaptation can
be ordinary typed flow nodes and are absent when they are not composed into the
blueprint.

Reference categories remain meaningful at the entrance: `const&` can be
borrowed until synchronous encoding completes, while `&&` transfers ownership.
After the boundary, frame and decoded values move through the flow. Fan-out is
the point where ownership must become explicit because multiple branches may
need shared or copied payloads.

The benchmark below measures both the blocking adapter and the GLib reactor.
Its GLib latency case drives the owning `GMainContext` and waits for each result
before launching the next request, so it is not a concurrent-throughput result.
A throughput comparison still requires matched cancellation, scheduling,
concurrency, and allocation semantics. The expected advantage is not removal
of network latency; it is that static dispatch, codec stages, and optional RPC
policies compose without a per-call general-purpose RPC object model.

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

The current tests cover fake, RPC, Python, and N-API paths. The RPC test covers
framing, ordinary functions, functors, lambdas, strict overload selection,
`void`, unknown methods, and malformed requests. The stub N-API test uses
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
cmake --build /tmp/dynabridge-cmake-bench --target rpc_call_benchmark
cmake --build /tmp/dynabridge-cmake-bench --target flow_composition_benchmark
```

Flux Foundry-enabled targets detect a sibling `../flux_foundry` checkout
automatically. Use `-DFLUX_FOUNDRY_INCLUDE_DIR=/path/to/flux_foundry` when it
lives elsewhere. Multi-config generators place executables under a
configuration subdirectory such as `build/Release`.

### Flux Foundry Tuning

`flow_composition_benchmark` isolates composition from RPC and I/O. It compares
the same 20-step integer pipeline as direct C++, a Flux Foundry `fast_runner`,
the same runner with one `fast_awaitable` that resumes synchronously from
`submit()` under owning and non-owning blueprint policies, and a C++20 coroutine
with one reusable frame, twenty ready `co_await` expressions, and one real
resume per call. Only this benchmark target requires C++20; dynabridge and Flux
Foundry remain C++14 libraries.

On the Raspberry Pi 4 environment described below, three-run medians were:

| 20-step synchronous pipeline | ns/call |
| --- | ---: |
| Direct C++ (forced inline) | 22.36 |
| Flux Foundry `fast_runner` | 22.44 |
| Flux Foundry owning runner + synchronous resume | 171.22 |
| Flux Foundry runner view + synchronous resume | 116.92 |
| C++20 coroutine (reused frame) | 32.12 |

Direct C++ and `fast_runner` are indistinguishable at this resolution, showing
that the typed synchronous nodes fused away. The synchronous-resume row measures
a real FF await boundary: pooled awaitable creation, continuation installation,
reference counting, inline resume, and result delivery all occur on every call.
The owning runner additionally keeps its blueprint alive through the
continuation; the view removes that ownership bookkeeping when the caller can
guarantee blueprint lifetime. The coroutine number is a strong best-case
baseline with no per-call frame allocation, so these rows describe different
execution contracts rather than claiming that every FF graph is faster than
every coroutine.

### RPC

On Linux, installing the `gio-2.0` development package (for example,
`libglib2.0-dev` on Debian) enables the nonblocking GLib transport.

Run the RPC benchmark with an optional local-iteration count:

```sh
/tmp/dynabridge-cmake-bench/rpc_call_benchmark 100000
```

It always measures direct C++ and the fully framed in-memory transport. On
POSIX it also measures a persistent localhost TCP transport. If rpclib headers
and `librpc` are found at configure time, the same executable adds an rpclib
TCP case using one server worker and the same `int(int, unsigned)` operation.

One Raspberry Pi 4 run (Cortex-A72 at 1.8 GHz, Debian aarch64, GCC 14.2,
Release, 100,000 local and 10,000 scalar/small-payload TCP calls) produced
these three-run median times:

| TCP workload | dynabridge | FF blocking | FF GLib | rpclib | GLib vs rpclib |
| --- | ---: | ---: | ---: | ---: | ---: |
| scalar `int(int, unsigned)` | 52,685 | 53,432 | 87,895 | 93,962 | 6% lower |
| 16 B string | 54,022 | 55,207 | 87,919 | 96,507 | 9% lower |
| 1 KiB string | 58,018 | 58,523 | 91,900 | 102,878 | 11% lower |
| 64 KiB string | 267,717 | 429,819 | 479,373 | 629,619 | 24% lower |

Times are ns/call. Blocking FF completion stays close to direct dynabridge
through 1 KiB. The GLib path additionally includes main-context polling and
GSource dispatch, then resumes the continuation inline; it remained 6-24%
below rpclib across these payloads. The framed scalar loopback path measured
about 523 ns/call without socket I/O in this run.

The benchmark also contains a handwritten asynchronous callback state machine
using the exact same GLib transport, codec, heap-allocated operation state, and
acquire/release completion signal. It is measured against the FF runner in
alternating AB/BA order on one connection to reduce scheduling and frequency
bias. Three-run medians isolate the composition overhead:

| Workload | Handwritten GLib | FF GLib | FF delta |
| --- | ---: | ---: | ---: |
| scalar `int(int, unsigned)` | 86,633 | 87,895 | 1,262 (1.5%) |
| 16 B string | 86,834 | 87,919 | 1,086 (1.3%) |
| 1 KiB string | 90,472 | 91,900 | 1,428 (1.6%) |
| 64 KiB string | 355,105 | 479,373 | 124,269 (35.0%) |

The small-call result puts FF's measured orchestration cost near one
microsecond on this machine. The 64 KiB delta is payload-sensitive rather than
a fixed scheduler cost and remains a profiling target for the benchmark
adapter's ownership and result-delivery path.

This is a latency microbenchmark, not an RPC feature comparison. Dynabridge's
demo protocol is deliberately narrow; rpclib provides a broader MsgPack RPC
protocol. Network topology, payload size, concurrency, backpressure, timeouts,
and production error handling can dominate real workloads.

### Python and Node.js

Set `DYNABRIDGE_BENCH_ITERS` to control the timed loop length.

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

#### Argument channels

The benchmarks also compare value, object, member-receiver, and callable
channels. One Raspberry Pi 4 run (Cortex-A72, Debian aarch64, GCC 14.2,
Python 3.13.5, Node.js 24.19.0, Release) used three timed runs of 500,000 calls;
the tables report medians. Relative cost uses the value call in the same
direction as its baseline.

| Python path | ns/call | Relative |
| --- | ---: | ---: |
| C++ -> Python value | 187.3 | 1.00x |
| C++ -> Python borrowed object | 204.3 | 1.09x |
| C++ -> Python fresh callback | 399.3 | 2.13x |
| Python -> C++ value (vectorcall) | 98.5 | 1.00x |
| Python -> C++ checked object argument | 104.0 | 1.06x |
| Python -> C++ member receiver | 108.0 | 1.10x |
| Python -> C++ existing callback | 302.7 | 3.07x |
| Python -> C++ construct object | 368.5 | 3.74x |

| Node.js path | ns/call | Relative |
| --- | ---: | ---: |
| C++ -> JavaScript value | 333.9 | 1.00x |
| C++ -> JavaScript borrowed object | 323.1 | 0.97x |
| C++ -> JavaScript fresh callback | 11,660.2 | 34.92x |
| JavaScript -> C++ value | 138.2 | 1.00x |
| JavaScript -> C++ checked object argument | 697.5 | 5.05x |
| JavaScript -> C++ checked member receiver | 699.4 | 5.06x |
| JavaScript -> C++ trusted member receiver | 411.0 | 2.97x |
| JavaScript -> C++ existing callback | 513.2 | 3.71x |
| JavaScript -> C++ construct object | 5,021.6 | 36.34x |

Borrowed import objects are close to value calls because the backend forwards
an existing handle without conversion or allocation. Exported object arguments
and N-API member receivers pay strict runtime class validation plus native-proxy
lookup. The Python extension layout makes both operations fixed-offset pointer
access.

The callback rows intentionally measure different lifecycle operations. An
existing dynamic callback passed into C++ is borrowed and immediately invoked.
A C++ callback passed into the dynamic runtime must create a fresh `PyObject`
or `napi_value` function on every measured call, then cross the boundary again;
N-API function creation dominates that result. Construction includes dynamic
object allocation, proxy allocation/binding, and finalization setup, so it is a
low-frequency lifecycle measurement rather than dispatch overhead.

#### Object and callback framework comparison

The same Raspberry Pi 4 setup was used to compare the new channels with raw
runtime APIs and established C++ wrappers. These are three-run medians from
500,000 calls with pybind11 2.13.6, nanobind 2.12.0, and node-addon-api 8.9.2.
Lower is better; `--` means the benchmark does not provide an equivalent raw
Python class implementation.

| Python path (ns/call) | Raw C API | dynabridge | pybind11 | nanobind |
| --- | ---: | ---: | ---: | ---: |
| Export scalar callable (tuple) | -- | 156.6 | 614.0 | 181.1 |
| Export scalar callable (vectorcall) | -- | 98.5 | -- | 116.9 |
| Import borrowed object | 253.9 | 204.3 | 386.9 | 291.9 |
| Import fresh callback | 482.5 | 399.3 | 3,056.4 | 801.6 |
| Export class argument | -- | 104.0 | 660.3 | 113.5 |
| Export class member | -- | 108.0 | 715.0 | 128.0 |
| Export existing callback | -- | 302.7 | 1,138.4 | 432.2 |
| Export construction | -- | 368.5 | 1,877.8 | 212.5 |

| Node.js path (ns/call) | Raw N-API | dynabridge | node-addon-api |
| --- | ---: | ---: | ---: |
| Export value | 121.4 | 138.2 | 167.1 |
| Export class argument | 426.7 | 697.5 | 440.3 |
| Export checked class member | -- | 699.4 | -- |
| Export trusted/direct class member | 359.6 | 411.0 | 473.0 |
| Export existing callback | 410.8 | 513.2 | 427.5 |
| Export construction | 3,344.0 | 5,021.6 | 3,785.1 |
| Import value | 368.8 | 333.9 | 346.3 |
| Import borrowed object | 321.8 | 323.1 | 318.6 |
| Import fresh callback | 9,819.9 | 11,660.2 | 12,982.0 |

The Python backend now uses the same broad object strategy as nanobind: a
dedicated extension type with native state at a fixed offset. Dynabridge's
class argument and member rows are slightly lower in this run. Construction is
still about 1.7x higher even after both implementations enter through type-level
vectorcall. Dynabridge reuses its generic constructor argument and object
wrappers, while nanobind's constructor caster placement-constructs the native
value directly. It is a lifecycle cost, not steady-state dispatch. On Node.js,
dynabridge validates a per-proxy Node-API type tag before unwrapping both class
arguments and member receivers. The handwritten N-API and node-addon-api rows
directly unwrap and cast the benchmark object, so they do not provide the same
wrong-receiver check. The explicit trusted policy removes that check and stays
close to raw N-API while remaining faster than node-addon-api in this run. The
typed callable channel requires neither a runtime registry nor a type-erased
callback wrapper.

Reference runs from two local platforms are shown below. Compare rows within the
same platform; absolute `ns/call` values depend on CPU, compiler, runtime, and
library versions.

#### Windows x64

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

#### Raspberry Pi aarch64 Linux

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
