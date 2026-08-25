'use strict';

const assert = require('node:assert/strict');

const addonPath = process.argv[2];
if (!addonPath) {
  throw new Error('usage: node node_call_benchmark.js <addon.node>');
}

const addon = require(addonPath);
const iterations = Number.parseInt(process.env.DYNABRIDGE_BENCH_ITERS || '1000000', 10);

function jsAdd(a, b) {
  return a + b;
}

function passCounter(_object, value) {
  return value + 2;
}

function passTransform(fn, value) {
  return fn(value);
}

function callback(value) {
  return value + 2;
}

function runJsLoop1(name, fn, a, expected) {
  let checksum = 0;
  for (let i = 0; i < 1000; ++i) {
    checksum += fn(a);
  }

  const start = process.hrtime.bigint();
  for (let i = 0; i < iterations; ++i) {
    checksum += fn(a);
  }
  const stop = process.hrtime.bigint();

  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: expected * (iterations + 1000),
  };
}

function runJsLoop2(name, fn, a, b, expected) {
  let checksum = 0;
  for (let i = 0; i < 1000; ++i) {
    checksum += fn(a, b);
  }

  const start = process.hrtime.bigint();
  for (let i = 0; i < iterations; ++i) {
    checksum += fn(a, b);
  }
  const stop = process.hrtime.bigint();

  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: expected * (iterations + 1000),
  };
}

function runNativeLoop(name, fn) {
  assert.equal(fn(jsAdd, 1000), 3000);

  const start = process.hrtime.bigint();
  const checksum = fn(jsAdd, iterations);
  const stop = process.hrtime.bigint();

  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: iterations * 3,
  };
}

function runNativeObjectLoop(name, fn) {
  const object = { value: 2 };
  assert.equal(fn(passCounter, object, 1000), 3000);

  const start = process.hrtime.bigint();
  const checksum = fn(passCounter, object, iterations);
  const stop = process.hrtime.bigint();

  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: iterations * 3,
  };
}

function runNativeCallbackLoop(name, fn) {
  assert.equal(fn(passTransform, 1000), 3000);

  const start = process.hrtime.bigint();
  const checksum = fn(passTransform, iterations);
  const stop = process.hrtime.bigint();

  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: iterations * 3,
  };
}

function runConstructLoop(name, Constructor) {
  let checksum = 0;
  let object;
  for (let i = 0; i < 1000; ++i) {
    object = new Constructor(2);
    checksum += 3;
  }

  const start = process.hrtime.bigint();
  for (let i = 0; i < iterations; ++i) {
    object = new Constructor(2);
    checksum += 3;
  }
  const stop = process.hrtime.bigint();

  assert.equal(object.value(), 2);
  return {
    name,
    ns: Number(stop - start),
    checksum,
    expectedChecksum: 3 * (iterations + 1000),
  };
}

function printResults(results) {
  console.log(`iterations: ${iterations}`);
  console.log(`node: ${process.version}`);
  console.log(`node-addon-api: ${addon.hasNodeAddonApi ? 'enabled' : 'not found at configure time'}`);
  console.log();
  console.log(`${'case'.padEnd(40)}${'ns/call'.padStart(14)}${'calls/sec'.padStart(14)}${'checksum'.padStart(14)}`);

  for (const result of results) {
    const nsPerCall = result.ns / iterations;
    const callsPerSec = 1_000_000_000 / nsPerCall;
    console.log(
      `${result.name.padEnd(40)}` +
      `${nsPerCall.toFixed(1).padStart(14)}` +
      `${callsPerSec.toFixed(0).padStart(14)}` +
      `${String(result.checksum).padStart(14)}`
    );
  }
}

const exportResults = [
  runJsLoop2('raw N-API export', addon.rawAdd, 1, 2, 3),
  runJsLoop2(
    'raw N-API export class argument',
    addon.rawConsumeCounter,
    addon.rawBenchmarkCounter,
    1,
    3
  ),
  runJsLoop1(
    'raw N-API export class member',
    addon.rawBenchmarkCounter.add.bind(addon.rawBenchmarkCounter),
    1,
    3
  ),
  runJsLoop2('raw N-API export callback', addon.rawUseCallback, callback, 1, 3),
  runConstructLoop('raw N-API export construct', addon.rawCounter),
  runJsLoop1('raw N-API overload export 1', addon.rawCalc, 1, 10),
  runJsLoop2('raw N-API overload export 2', addon.rawCalc, 1, 2, 3),
  runJsLoop2('dynabridge export', addon.dynabridgeAdd, 1, 2, 3),
  runJsLoop2('dynabridge export class argument', addon.consume_counter, addon.benchmarkCounter, 1, 3),
  runJsLoop1(
    'dynabridge export class member',
    addon.benchmarkCounter.add.bind(addon.benchmarkCounter),
    1,
    3
  ),
  runJsLoop1(
    'dynabridge trusted class member',
    addon.trustedBenchmarkCounter.add.bind(addon.trustedBenchmarkCounter),
    1,
    3
  ),
  runJsLoop2('dynabridge export callback', addon.use_callback, callback, 1, 3),
  runConstructLoop('dynabridge export construct', addon.counter),
  runJsLoop1('dynabridge overload export 1', addon.calc, 1, 10),
  runJsLoop2('dynabridge overload export 2', addon.calc, 1, 2, 3),
];

if (addon.hasNodeAddonApi) {
  exportResults.push(runJsLoop2('node-addon-api export', addon.nodeAddonApiAdd, 1, 2, 3));
  exportResults.push(runJsLoop2(
    'node-addon-api export class argument',
    addon.nodeAddonApiConsumeCounter,
    addon.nodeAddonApiBenchmarkCounter,
    1,
    3
  ));
  exportResults.push(runJsLoop1(
    'node-addon-api export class member',
    addon.nodeAddonApiBenchmarkCounter.add.bind(addon.nodeAddonApiBenchmarkCounter),
    1,
    3
  ));
  exportResults.push(runJsLoop2(
    'node-addon-api export callback',
    addon.nodeAddonApiUseCallback,
    callback,
    1,
    3
  ));
  exportResults.push(runConstructLoop(
    'node-addon-api export construct',
    addon.nodeAddonApiCounter
  ));
  exportResults.push(runJsLoop1('node-addon-api overload export 1', addon.nodeAddonApiCalc, 1, 10));
  exportResults.push(runJsLoop2('node-addon-api overload export 2', addon.nodeAddonApiCalc, 1, 2, 3));
}

const importResults = [
  runNativeLoop('raw N-API import', addon.rawCallLoop),
  runNativeObjectLoop('raw N-API import object', addon.rawObjectCallLoop),
  runNativeCallbackLoop('raw N-API import callback', addon.rawCallbackCallLoop),
  runNativeLoop('dynabridge import', addon.dynabridgeCallLoop),
  runNativeObjectLoop('dynabridge import object', addon.dynabridgeObjectCallLoop),
  runNativeCallbackLoop('dynabridge import callback', addon.dynabridgeCallbackCallLoop),
];

if (addon.hasNodeAddonApi) {
  importResults.push(runNativeLoop('node-addon-api import', addon.nodeAddonApiCallLoop));
  importResults.push(runNativeObjectLoop(
    'node-addon-api import object',
    addon.nodeAddonApiObjectCallLoop
  ));
  importResults.push(runNativeCallbackLoop(
    'node-addon-api import callback',
    addon.nodeAddonApiCallbackCallLoop
  ));
}

const results = exportResults.concat(importResults);

for (const result of results) {
  assert.equal(result.checksum, result.expectedChecksum);
}

printResults(results);
