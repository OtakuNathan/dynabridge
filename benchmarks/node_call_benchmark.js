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

function printResults(results) {
  console.log(`iterations: ${iterations}`);
  console.log(`node: ${process.version}`);
  console.log(`node-addon-api: ${addon.hasNodeAddonApi ? 'enabled' : 'not found at configure time'}`);
  console.log();
  console.log(`${'case'.padEnd(32)}${'ns/call'.padStart(14)}${'calls/sec'.padStart(14)}${'checksum'.padStart(14)}`);

  for (const result of results) {
    const nsPerCall = result.ns / iterations;
    const callsPerSec = 1_000_000_000 / nsPerCall;
    console.log(
      `${result.name.padEnd(32)}` +
      `${nsPerCall.toFixed(1).padStart(14)}` +
      `${callsPerSec.toFixed(0).padStart(14)}` +
      `${String(result.checksum).padStart(14)}`
    );
  }
}

const exportResults = [
  runJsLoop2('raw N-API export', addon.rawAdd, 1, 2, 3),
  runJsLoop1('raw N-API overload export 1', addon.rawCalc, 1, 10),
  runJsLoop2('raw N-API overload export 2', addon.rawCalc, 1, 2, 3),
  runJsLoop2('dynabridge export', addon.dynabridgeAdd, 1, 2, 3),
  runJsLoop1('dynabridge overload export 1', addon.calc, 1, 10),
  runJsLoop2('dynabridge overload export 2', addon.calc, 1, 2, 3),
];

if (addon.hasNodeAddonApi) {
  exportResults.push(runJsLoop2('node-addon-api export', addon.nodeAddonApiAdd, 1, 2, 3));
  exportResults.push(runJsLoop1('node-addon-api overload export 1', addon.nodeAddonApiCalc, 1, 10));
  exportResults.push(runJsLoop2('node-addon-api overload export 2', addon.nodeAddonApiCalc, 1, 2, 3));
}

const importResults = [
  runNativeLoop('raw N-API import', addon.rawCallLoop),
  runNativeLoop('dynabridge import', addon.dynabridgeCallLoop),
];

if (addon.hasNodeAddonApi) {
  importResults.push(runNativeLoop('node-addon-api import', addon.nodeAddonApiCallLoop));
}

const results = exportResults.concat(importResults);

for (const result of results) {
  assert.equal(result.checksum, result.expectedChecksum);
}

printResults(results);
