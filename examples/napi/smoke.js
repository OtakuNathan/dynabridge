const assert = require("assert");

const addonPath = process.argv[2];
assert.ok(addonPath, "expected addon path");

const addon = require(addonPath);
assert.strictEqual(addon.add(12, 13), 25);

const counter = new addon.counter(13);
assert.strictEqual(counter.add(29), 42);
assert.strictEqual(counter.value(), 13);
