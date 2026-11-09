# Replay composition: Dyna Bridge, FF, and lease

This complete example demonstrates three separate responsibilities:

```text
Python call
  -> Dyna Bridge: declared callable, CPython operations, string conversion
  -> FF: validate/fill/mark, then calculate the next decision
  -> lease: coordinator retains write authority and lends read authority to policy
  -> JSON string back to Python
```

The policy is an ordinary calculation callable. There is no runtime callback per
observation, executor handoff, or cancellation requirement. FF uses a borrowed
fast runner; lease uses bare, single-threaded access. Dyna Bridge does not own
scheduling or state-access policy, and neither FF nor lease depends on Python.

## Sources and semantics

All sources are in [`examples/replay/`](examples/replay/):

| File | Responsibility |
| --- | --- |
| [`api.hpp`](examples/replay/api.hpp) | Narrow string-in/string-out application boundary; no FF, lease, or Python types. |
| [`core.cpp`](examples/replay/core.cpp) | CSV parsing, enum errors, FF steps, lease handoff, and result serialization. |
| [`exports.def`](examples/replay/exports.def) | Declares `run_arena`'s allowed signature. |
| [`module.cpp`](examples/replay/module.cpp) | CPython registration through an explicit-signature lambda. |

The deliberately small input format is a one-column CSV headed `price` with LF
line endings, at most 256 observations and 64 KiB. Config is initial cash as
numeric text in `(0, 1000000]`. Each positive finite price must be in
`[0.000001, 1000000000]`. The example uses the C numeric locale supplied by the
process (normally `C`); it does not change global locale or define a portable
market-data format.

At each observation, fill the **previous** pending buy at the current price,
mark the account, then ask the policy for the next cash budget (up to 100).
The final pending order is reported and left unfilled. This is a documentation
fixture, with fractional units and no fees, interest, monthly funding, or exchange
rules; it is not the full strategy arena or evidence of trading performance.

The coordinator creates one writer for stack-resident account storage. The
policy accepts `shared_access<account>` by value and returns a numeric budget.
Every reader dies before the next write. All callbacks complete synchronously,
and no handle or raw alias escapes the call. Ordinary parse/domain failures
return JSON containing `error`; the FF receiver observes both success and
error. The final JSON is created after replay, not once per event.

## Build a no-exception core and a separate Python boundary

The following Linux commands assume checkouts named `bridge`, `flux_foundry`,
and `lease` under the same parent directory. Run from the **bridge repository
root**, adjusting include paths for your layout. CPython development headers
and the matching `python3-config` are required.

```sh
mkdir -p build-docs/replay

# FF and lease live only in this translation unit.
c++ -std=c++14 -O2 -DNDEBUG -fPIC -fno-exceptions \
  -DFLUX_FOUNDRY_NO_EXCEPTION_STRICT=1 -I../flux_foundry -I../lease/lease \
  -c docs/examples/replay/core.cpp -o build-docs/replay/core.o

# Keep exceptions enabled for the CPython backend and its converters.
c++ -std=c++14 -O2 -DNDEBUG -fPIC -I. $(python3-config --includes) \
  -c docs/examples/replay/module.cpp -o build-docs/replay/module.o

c++ -shared build-docs/replay/core.o build-docs/replay/module.o -pthread \
  -o "build-docs/replay/replay_example$(python3-config --extension-suffix)"

PYTHONPATH=build-docs/replay python3 - <<'PY'
import json
import replay_example

result = json.loads(replay_example.run_arena("price\n100\n110\n90\n", "1000"))
assert result["observations"] == 3 and result["fills"] == 2
assert result["cash"] == 800 and result["pending_budget"] == 100
assert abs(result["equity"] - (800 + (100 / 110 + 100 / 90) * 90)) < 1e-7
assert json.loads(replay_example.run_arena("price\n0\n", "1000"))["error"] == "invalid_price"
assert json.loads(replay_example.run_arena("price\n", "1000"))["error"] == "empty_csv"
assert json.loads(replay_example.run_arena("price\n100\n", "bad"))["error"] == "invalid_config"
print(result)
PY
```

This split is **an application configuration choice**, not a requirement that
all three libraries or every backend disable exceptions. The enum-only `then`
in this core uses FF's no-exception path; CPython runtime failures still use its
backend's exception handling. The two translation units share only the simple
application declaration. Avoid instantiating the same header-only library with
conflicting configuration across translation units; use a consistent compiler,
standard-library ABI, and library configuration wherever those definitions are
shared. Do not combine these files into a unity translation unit with one set
of exception flags.

`noexcept` does not recover allocation failure: constructing strings and lease
Debug/shared control blocks may allocate, and exhaustion in the no-exception
core is fatal. Domain errors returned by the example are separate from that
resource-failure contract. As in the minimal module template, the static context
is for a normal interpreter lifetime, not subinterpreter isolation or repeated
interpreter reinitialization. Calls retain the Python GIL.

## Related selection guides

- [Dyna Bridge callable exports](callable_exports.md)
- [FF runner/node selection and synchronous status-code example](https://github.com/OtakuNathan/flux_foundry/blob/main/docs/synchronous_hot_path.md)
- [lease component handoff and guarantee boundary](https://github.com/OtakuNathan/lease/blob/main/docs/component_handoff.md)

The standalone examples in the two other repositories do not require this
integration or any Python dependency.
