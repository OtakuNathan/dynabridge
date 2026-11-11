// Isolate construction from other benchmark cases and drain finalizers between
// runs. The timed region includes retaining each object, but excludes method
// validation and teardown. Run each addon variant in a separate Node process.
const assert = require('assert');
const addon = require(process.argv[2]);
const iterations = Number(process.env.PORTAL_BENCH_ITERS || 100000);
assert.ok(Number.isSafeInteger(iterations) && iterations > 0);
assert.equal(typeof global.gc, 'function', 'run Node with --expose-gc');

async function collect() {
  for (let i = 0; i < 8; ++i) {
    global.gc();
    await new Promise(resolve => setImmediate(resolve));
  }
}

function allocate(Constructor, count) {
  const objects = new Array(count);
  const startMs = Date.now();
  const start = process.hrtime.bigint();
  for (let i = 0; i < count; ++i) objects[i] = new Constructor(2);
  const ns = Number(process.hrtime.bigint() - start) / count;
  const endMs = Date.now();
  let checksum = 0;
  for (const object of objects) checksum += object.value();
  assert.equal(checksum, count * 2);
  return {ns, startMs, endMs};
}

async function main() {
  for (const [name, Constructor] of [['raw', addon.rawCounter], ['portal', addon.counter]]) {
    allocate(Constructor, 1000);
    await collect();
    const result = allocate(Constructor, iterations);
    console.log(JSON.stringify({name, iterations, ...result}));
    await collect();
  }
  // An unchanged call path serves as a control, with a longer warmup/loop than
  // the general benchmark's default small export case.
  let checksum = 0;
  for (let i = 0; i < 100000; ++i) checksum += addon.portalAdd(1, 2);
  const startMs = Date.now();
  const start = process.hrtime.bigint();
  for (let i = 0; i < 2000000; ++i) checksum += addon.portalAdd(1, 2);
  const ns = Number(process.hrtime.bigint() - start) / 2000000;
  const endMs = Date.now();
  assert.equal(checksum, 6300000);
  console.log(JSON.stringify({name: 'export control', ns, startMs, endMs}));
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
