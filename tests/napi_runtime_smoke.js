const assert = require("assert");

const addonPath = process.argv[2];
assert.ok(addonPath, "expected addon path");

const addon = require(addonPath);

async function main() {
    assert.strictEqual(addon.add(12, 13), 25);
    assert.strictEqual(addon.calc(6), 60);
    assert.strictEqual(addon.calc(6, 7), 42);
    assert.strictEqual(addon.multiply(6, 7), 42);

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

    const consumer = new addon.consumer(counter, (value) => value * 3);
    assert.strictEqual(consumer.combine(counter, 11), 63);
    assert.strictEqual(consumer.apply((value) => value * 3, 7), 60);
    assert.throws(() => addon.consume_counter(consumer, 1));
    assert.throws(() => counter.add.call(consumer, 1));

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
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
