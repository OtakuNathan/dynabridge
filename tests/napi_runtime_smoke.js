const assert = require("assert");

const addonPath = process.argv[2];
assert.ok(addonPath, "expected addon path");

const addon = require(addonPath);

async function main() {
    assert.strictEqual(addon.add(12, 13), 25);
    assert.strictEqual(addon.calc(6), 60);
    assert.strictEqual(addon.calc(6, 7), 42);
    assert.strictEqual(addon.multiply(6, 7), 42);
    assert.strictEqual(addon.echo("héllo 中"), "<héllo 中>");
    assert.strictEqual(addon.echo("a\0b"), "<a\0b>");
    assert.strictEqual(addon.echo(21), 42);
    for (const value of [1.9, -0.5, NaN, Infinity, -Infinity, 4294967296, -2147483649]) {
        assert.throws(() => addon.echo(value), `int accepted ${value}`);
    }
    for (const value of [1.9, -0.5, -1, NaN, Infinity, 4294967296]) {
        assert.throws(() => new addon.counter(value), `unsigned accepted ${value}`);
    }
    assert.strictEqual(addon.add(2147483647, 0), 2147483647);
    assert.strictEqual(addon.add(-2147483648, 0), -2147483648);
    assert.strictEqual(addon.add(-0, 0), 0);
    for (const value of [-2147483648, -1, -0, 0, 1, 2147483647]) {
        assert.strictEqual(addon.identityInt(value), value === 0 ? 0 : value);
    }
    for (const value of [-0, 0, 1, 2147483648, 4294967295]) {
        assert.strictEqual(addon.identityUnsigned(value), value === 0 ? 0 : value);
    }
    for (const value of [
        Number.MIN_VALUE, -Number.MIN_VALUE, 0.5, -0.5, 2147483646.5,
        -2147483647.5, NaN, Infinity, -Infinity, 4294967296,
        null, undefined, true, "1", {}, [], new Number(1), 1n, Symbol("1"),
    ]) {
        assert.throws(() => addon.identityInt(value));
        assert.throws(() => addon.identityUnsigned(value));
    }
    for (const value of [-1, -2147483648, 4294967294.5]) {
        assert.throws(() => addon.identityUnsigned(value));
    }
    for (const value of [-2147483649, 2147483648, 4294967295]) {
        assert.throws(() => addon.identityInt(value));
    }
    for (const text of ["", "\0", "x".repeat(65536) + "\0😀", "😀中é"]) {
        assert.strictEqual(addon.echo(text), "<" + text + ">");
    }
    // Keep Node's existing UTF-8 replacement behavior for unpaired surrogates.
    assert.strictEqual(addon.echo("\ud800"), "<\ufffd>");
    for (const invoke of [
        () => addon.add(), () => addon.add(1), () => addon.add(1, 2, 3),
        () => addon.calc(), () => addon.calc(1, 2, 3),
        () => addon.stored(1), () => new addon.counter(),
        () => new addon.counter(1, 2),
    ]) {
        assert.throws(invoke);
    }

    addon.store(77);
    assert.strictEqual(addon.stored(), 77);

    assert.strictEqual(addon.ownedCounterConstructed(), 0);
    assert.strictEqual(addon.ownedCounterDestroyed(), 0);

    let counter = new addon.counter(13);
    assert.strictEqual(addon.ownedCounterConstructed(), 1);
    assert.strictEqual(counter.add(29), 42);
    assert.strictEqual(counter.value(), 13);
    assert.strictEqual(addon.consume_counter(counter, 11), 24);
    assert.strictEqual(addon.use_callback((value) => value * 3, 7), 22);
    assert.throws(() => addon.consume_counter(7, 1));
    assert.throws(() => addon.use_callback(7, 1));

    const consumer = new addon.consumer(counter, (value) => {
        // Constructor argument storage must survive a callback re-entering a
        // constructor of the same class before the outer native object exists.
        const nested = new addon.consumer(counter, (inner) => inner * 2);
        assert.strictEqual(nested.apply((inner) => inner + 1, 0), 27);
        return value * 3;
    });
    assert.strictEqual(consumer.combine(counter, 11), 63);
    assert.strictEqual(consumer.apply((value) => value * 3, 7), 60);
    assert.throws(() => addon.consume_counter(consumer, 1));
    assert.throws(() => counter.add.call(consumer, 1));
    assert.throws(() => counter.value(1));
    assert.throws(() => counter.add());
    assert.throws(() => counter.add(1, 2));
    assert.throws(() => new addon.consumer(counter));
    assert.throws(() => new addon.consumer(counter, (value) => value, 1));
    const callbackError = new Error("callback identity");
    assert.throws(
        () => addon.use_callback(() => { throw callbackError; }, 1),
        (error) => error === callbackError);
    assert.throws(
        () => new addon.consumer(counter, () => { throw callbackError; }),
        (error) => error === callbackError);

    let calcArgs = null;
    assert.strictEqual(
        addon.callImportedCalc((a, b) => {
            calcArgs = [a, b];
            return a + b;
        }, 3, 4),
        7);
    assert.deepStrictEqual(calcArgs, [3, 4]);

    let fooArgs = null;
    assert.strictEqual(
        addon.callImportedFoo((a, b) => {
            fooArgs = [a, b];
        }, 5, 6),
        undefined);
    assert.deepStrictEqual(fooArgs, [5, 6]);

    assert.strictEqual(
        addon.callImportedEcho((text) => "[" + text + "]", "héllo 中"),
        "[héllo 中]");
    assert.strictEqual(
        addon.callImportedEcho((text) => "[" + text + "]", "a\0b"),
        "[a\0b]");

    const foreignCounter = { handle: 13 };
    function counterDispatch(receiver, value) {
        if (typeof receiver === "number") {
            return { handle: receiver };
        }
        if (value === undefined) {
            return receiver.handle;
        }
        return receiver.handle + value;
    }

    assert.strictEqual(addon.callImportedCounterAdd(counterDispatch, foreignCounter, 29), 42);
    assert.strictEqual(addon.callImportedCounterValue(counterDispatch, foreignCounter), 13);
    assert.strictEqual(addon.constructImportedCounterAdd(counterDispatch, 21, 21), 42);
    assert.strictEqual(
        addon.callImportedPassCounter((obj, value) => obj.handle + value, foreignCounter, 29),
        42);
    assert.strictEqual(
        addon.callImportedPassTransform((fn, value) => fn(value), 4),
        40);

    counter = null;
    for (let i = 0; i < 8; ++i) {
        global.gc();
        await new Promise((resolve) => setImmediate(resolve));
    }
    assert.strictEqual(addon.ownedCounterDestroyed(), 1);

    // Constructor callbacks need no persistent C++ handle, but JS must still
    // retain each native object and release it exactly once after collection.
    const constructedBefore = addon.ownedCounterConstructed();
    const destroyedBefore = addon.ownedCounterDestroyed();
    function constructBatch() {
        const objects = Array.from({length: 256}, (_, i) => new addon.counter(i));
        global.gc();
        assert.strictEqual(addon.ownedCounterDestroyed(), destroyedBefore);
        for (let i = 0; i < objects.length; ++i) {
            assert.strictEqual(objects[i].value(), i);
            assert.strictEqual(objects[i].add(1), i + 1);
        }
    }
    constructBatch();
    assert.strictEqual(addon.ownedCounterConstructed(), constructedBefore + 256);
    for (let i = 0; i < 8; ++i) {
        global.gc();
        await new Promise((resolve) => setImmediate(resolve));
    }
    assert.strictEqual(addon.ownedCounterDestroyed(), destroyedBefore + 256);
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
