# Choosing a callable export

The `.def` contract declares the allowed signatures. Registration supplies an
implementation for those signatures. The implementation's C++ shape determines
which registration overload to use; the backend still supplies runtime operations
and converters still determine which value types may cross the boundary.

## Quick reference

| C++ implementation | Registration |
| --- | --- |
| Ordinary function or function pointer | `export_name(context, module, function)` deduces its signature. |
| Noncapturing lambda, capturing lambda, or function object | `export_name<Signature>(context, module, callable)` specifies the declared signature. |
| Separate implementations for multiple signatures | `export_name(context, module).bind<Signature>(implementation)...commit()` |
| Overloaded C++ function name | Select one function pointer with `static_cast<R(*)(Args...)>(function)`; the same cast also disambiguates an implementation passed to explicit-signature registration or `bind`. |

For a contract entry such as:

```cpp
BEGIN_CALLABLE_GROUP(run_arena)
    DECL_CALLABLE(std::string, const std::string&, const std::string&)
END_CALLABLE_GROUP
```

an ordinary lambda can be registered directly:

```cpp
using Signature =
    std::string(const std::string&, const std::string&);

portal::export_run_arena<Signature>(
    context, module,
    [](const std::string& csv, const std::string& config) {
        return arena::run_arena(csv, config);
    });
```

Unary `+` in `+[](...) { ... }` converts a **noncapturing** lambda to a function
pointer, allowing the pointer overload to deduce the signature. Exporting a
lambda itself does not require that conversion: the explicit-signature form
above also supports captures. A capturing lambda cannot use that pointer
conversion.

```cpp
const std::string prefix = "saved:";
portal::export_run_arena<Signature>(context, module,
    [prefix](const std::string& csv, const std::string& config) {
        return prefix + arena::run_arena(csv, config);
    });
```

This is an alternative registration for the same name, not an instruction to
register it twice. Prefer owning captures for retained state; reference captures
do not extend the referenced object's lifetime. The context used by generated
binders must also remain valid while exported callables use it.

For multiple signatures declared under one group:

```cpp
portal::export_calc(context, module)
    .bind<int(int)>([](int value) { return value * 10; })
    .bind<int(int, unsigned)>([](int value, unsigned factor) {
        return value * static_cast<int>(factor);
    })
    .commit();
```

`commit()` registers the completed group. Runtime overload selection checks
arity and strict conversions in **`.def` declaration order**, choosing the first
viable declaration. It is not C++ conversion ranking or binding-call order.
Signatures must be declared, callable shapes must be compatible, and the chosen
backend must have the needed converters. The generated `static_assert`
diagnostics describe contract/signature mismatches; an unresolved overloaded
C++ function name may instead need the explicit function-pointer cast above.

## Complete minimal CPython module

The runnable template has two source files:

- [`examples/callable_module/exports.def`](examples/callable_module/exports.def)
  declares every export, including both `calc` signatures.
- [`examples/callable_module/module.cpp`](examples/callable_module/module.cpp)
  implements and registers functions, both lambda forms, the `+[]` alternative,
  and the builder. `arena::run_arena` is a string stub in this template.

The registration path is:

1. Make declaration types visible (`<string>` here), select
   `PORTAL_EXPORT_DEF`, and include `portal/portal.h`. Default import and
   export tables are empty; no import table is needed for this module.
2. Include `portal/backends/python.h`. This selects CPython operations and
   includes its built-in `int`, `unsigned`, and `std::string` converters. Include
   `portal/backends/python_api.h` rather than `Python.h` directly if a separate
   file needs the Python API. New value types need a suitable backend converter.
3. Create the Python module, keep an export context alive, wrap the module as
   borrowed, and register the declared callables.
4. Return the new module reference on success. On initialization failure, release
   it, set a Python exception, and return null. The wrapper's borrowed reference
   policy does not consume the module's original reference.

On Linux, from the repository root with CPython development headers installed:

```sh
mkdir -p build-docs/callable
c++ -std=c++14 -O2 -fPIC -shared -I. $(python3-config --includes) \
  docs/examples/callable_module/module.cpp \
  -o "build-docs/callable/callable_example$(python3-config --extension-suffix)"
PYTHONPATH=build-docs/callable python3 - <<'PY'
import callable_example as module
assert module.deduced("csv", "config") == "csv:config"
assert module.run_arena("csv", "config") == "csv:config"
assert module.captured("csv", "config") == "saved:csv:config"
assert module.pointer_lambda("csv", "config") == "csv:config"
assert module.calc(4) == 40
assert module.calc(4, 3) == 12
try:
    module.run_arena(1, "config")
except TypeError:
    pass
else:
    raise AssertionError("wrong argument type was accepted")
print("callable exports: passed")
PY
```

Use the `python3-config` matching the interpreter that will import the module.
These are Linux commands; the repository's
[CMake Python example](../examples/python/module.cpp) handles platform-specific
module setup. This small static-context template follows the existing example;
it is not a design for interpreter teardown/reinitialization or isolated Python
subinterpreters. Calls execute with the caller's Python GIL held.

The CPython boundary uses exceptions for runtime failures. Core/RPC
[no-exception support](noexceptions.md) does not imply that this backend can be
compiled without exceptions. A separate [replay composition example](replay_composition.md)
shows a no-exception calculation core linked to an exception-enabled Python
translation unit.
