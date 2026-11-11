import gc
import importlib.util
import sys


def load_addon(path):
    spec = importlib.util.spec_from_file_location("portal_python_runtime_addon", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load extension module from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("usage: python_runtime_smoke.py <addon>")

    addon = load_addon(sys.argv[1])

    assert addon.add(12, 13) == 25
    assert addon.calc(6) == 60
    assert addon.calc(6, 7) == 42
    assert addon.multiply(6, 7) == 42
    assert addon.echo("héllo 中") == "<héllo 中>"
    assert addon.echo("a\0b") == "<a\0b>"
    assert addon.echo(21) == 42

    addon.store(77)
    assert addon.stored() == 77

    assert addon.ownedCounterConstructed() == 0
    assert addon.ownedCounterDestroyed() == 0

    counter = addon.counter(13)
    assert addon.ownedCounterConstructed() == 1
    assert counter.add(29) == 42
    assert counter.value() == 13

    calc_args = None

    def calc(a, b):
        nonlocal calc_args
        calc_args = [a, b]
        return int(a) + int(b)

    assert addon.callImportedCalc(calc, 3, 4) == 7
    assert calc_args == [3, 4]

    foo_args = None

    def foo(*args):
        nonlocal foo_args
        foo_args = list(args)

    assert addon.callImportedFoo(foo, 5, 6) is None
    assert foo_args == [5, 6]

    def echo_fn(text):
        return "[" + text + "]"

    assert addon.callImportedEcho(echo_fn, "héllo 中") == "[héllo 中]"
    assert addon.callImportedEcho(echo_fn, "a\0b") == "[a\0b]"

    class ForeignCounter:
        pass

    def make_counter(handle):
        result = ForeignCounter()
        result.handle = int(handle)
        return result

    def counter_dispatch(receiver_or_handle, *args):
        if not hasattr(receiver_or_handle, "handle"):
            return make_counter(receiver_or_handle)
        if not args:
            return int(receiver_or_handle.handle)
        return int(receiver_or_handle.handle) + int(args[0])

    foreign_counter = make_counter(13)
    assert addon.callImportedCounterAdd(counter_dispatch, foreign_counter, 29) == 42
    assert addon.callImportedCounterValue(counter_dispatch, foreign_counter) == 13
    assert addon.constructImportedCounterAdd(counter_dispatch, 21, 21) == 42

    # Hand-written C API wrappers must restore the owned exception too.
    original = ValueError("original callback failure", 123)

    def raising(*args):
        raise original

    for invoke in (
        lambda: addon.callImportedCalc(raising, 3, 4),
        lambda: addon.callImportedFoo(raising, 5, 6),
        lambda: addon.callImportedEcho(raising, "text"),
        lambda: addon.callImportedCounterAdd(raising, foreign_counter, 29),
        lambda: addon.callImportedCounterValue(raising, foreign_counter),
        lambda: addon.constructImportedCounterAdd(raising, 21, 21),
    ):
        original.__traceback__ = None
        try:
            invoke()
        except ValueError as error:
            assert error is original
            tb = error.__traceback__
            while tb.tb_next is not None:
                tb = tb.tb_next
            assert tb.tb_frame.f_code.co_name == "raising"
        else:
            raise AssertionError("import wrapper lost callback exception")
    original.__traceback__ = None
    assert addon.callImportedCalc(calc, 3, 4) == 7

    counter = None
    for _ in range(8):
        gc.collect()

    assert addon.ownedCounterDestroyed() == 1

    class Unrelated:
        __slots__ = tuple(f"s{i}" for i in range(16))

    for receiver in (Unrelated(), object(), None):
        try:
            addon.counter.__init__(receiver, 1)
        except TypeError:
            pass
        else:
            raise AssertionError("constructor accepted an unrelated receiver")

    class Derived(addon.counter):
        pass

    for cls in (addon.counter, Derived):
        before = sys.getrefcount(cls)
        for _ in range(1000):
            instance = cls(7)
            assert instance.add(2) == 9
            del instance
        gc.collect()
        assert sys.getrefcount(cls) == before, "instance leaked a type reference"

    instance = addon.counter(1)
    try:
        instance.__init__(2)
    except RuntimeError:
        pass
    else:
        raise AssertionError("constructor reinitialized a live native object")
    assert instance.value() == 1


if __name__ == "__main__":
    main()
