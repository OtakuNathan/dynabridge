import importlib.util
import sys


def load_addon(path):
    spec = importlib.util.spec_from_file_location("dynabridge_python_example", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load extension module from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


if len(sys.argv) != 2:
    raise RuntimeError("usage: smoke.py <addon>")

addon = load_addon(sys.argv[1])
assert addon.add(12, 13) == 25

counter = addon.counter(13)
assert counter.add(29) == 42
assert counter.value() == 13
