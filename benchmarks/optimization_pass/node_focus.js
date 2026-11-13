// Longer, warmed call loops without constructor/finalizer work between them.
// Use the same addon harness and script for every isolated implementation.
'use strict';
const assert = require('node:assert/strict');
const addon = require(process.argv[2]);
const count = Number(process.env.PORTAL_BENCH_ITERS || 1000000);
assert.ok(Number.isSafeInteger(count) && count > 0 && count <= 2000000);
function callback(value) { return value + 2; }

function numeric(name, fn, args, expected) {
  let checksum = 0;
  const warmup = Math.min(100000, count);
  for (let i = 0; i < warmup; ++i) {
    checksum += args.length === 1 ? fn(args[0]) : fn(args[0], args[1]);
  }
  const begin = process.hrtime.bigint();
  if (args.length === 1) {
    for (let i = 0; i < count; ++i) checksum += fn(args[0]);
  } else {
    for (let i = 0; i < count; ++i) checksum += fn(args[0], args[1]);
  }
  const ns = Number(process.hrtime.bigint() - begin) / count;
  assert.equal(checksum, expected * (warmup + count));
  console.log(JSON.stringify({name, iterations: count, ns, checksum}));
}

function string(name, text, iterations = count) {
  let checksum = 0;
  let result;
  const warmup = Math.min(100000, Math.max(100, Math.floor(iterations / 10)));
  for (let i = 0; i < warmup; ++i) {
    result = addon.portalIdentityString(text);
    checksum += result.length;
  }
  const begin = process.hrtime.bigint();
  for (let i = 0; i < iterations; ++i) {
    result = addon.portalIdentityString(text);
    checksum += result.length;
  }
  const ns = Number(process.hrtime.bigint() - begin) / iterations;
  assert.equal(result, text);
  assert.equal(checksum, text.length * (warmup + iterations));
  console.log(JSON.stringify({name, iterations, ns, checksum}));
}

numeric('raw export', addon.rawAdd, [1, 2], 3);
numeric('raw member', addon.rawBenchmarkCounter.add.bind(addon.rawBenchmarkCounter), [1], 3);
numeric('portal export', addon.portalAdd, [1, 2], 3);
numeric('portal member', addon.benchmarkCounter.add.bind(addon.benchmarkCounter), [1], 3);
numeric('portal callback', addon.use_callback, [callback, 1], 3);
numeric('portal strict unsigned wide', addon.portalIdentityUnsigned, [4294967295], 4294967295);
numeric('portal overload string miss', addon.echo, [1], 1);
string('portal UTF-8 empty', '');
string('portal UTF-8 16 B', 'x'.repeat(16));
string('portal UTF-8 Unicode + NUL', 'héllo\0中😀');
string('portal UTF-8 1 KiB', 'x'.repeat(1024));
string('portal UTF-8 64 KiB', 'x'.repeat(65536), Math.max(1, Math.floor(count / 64)));
