const assert = require("assert");
const { once } = require("events");
const { Worker, isMainThread, parentPort, workerData } = require("worker_threads");

function check(addon, retainedCounter) {
    assert.strictEqual(addon.add(12, 13), 25);
    assert.strictEqual(retainedCounter.add(29), 42);
    assert.strictEqual(retainedCounter.value(), 13);
    const counter = new addon.counter(17);
    assert.strictEqual(counter.add(25), 42);
    assert.strictEqual(counter.value(), 17);
}

if (!isMainThread) {
    const addon = require(workerData);
    // Keep a native object alive through both natural and forced shutdown.
    const retainedCounter = new addon.counter(13);
    check(addon, retainedCounter);
    parentPort.on("message", (message) => {
        if (message === "check") {
            for (let i = 0; i < 100; ++i) check(addon, retainedCounter);
            parentPort.postMessage("checked");
        } else if (message === "close") {
            parentPort.close();
        }
    });
    parentPort.postMessage("ready");
} else {
    async function main() {
        const addonPath = process.argv[2];
        assert.ok(addonPath, "expected addon path");
        const addon = require(addonPath);
        const retainedCounter = new addon.counter(13);
        check(addon, retainedCounter);

        // Reinitialization in one thread must not replace older exports' state.
        delete require.cache[require.resolve(addonPath)];
        const reloaded = require(addonPath);
        const reloadedCounter = new reloaded.counter(13);
        check(addon, retainedCounter);
        check(reloaded, reloadedCounter);

        for (let round = 0; round < 3; ++round) {
            const workers = [];
            try {
                const ready = [];
                for (let i = 0; i < 4; ++i) {
                    const worker = new Worker(__filename, { workerData: addonPath });
                    workers.push(worker);
                    ready.push(once(worker, "message"));
                }
                for (const [message] of await Promise.all(ready)) {
                    assert.strictEqual(message, "ready");
                }
                // All Workers loaded the addon before the main thread calls it.
                // A process-wide static context aborts here (or during loading).
                check(addon, retainedCounter);
                check(reloaded, reloadedCounter);
                const checked = workers.map((worker) => {
                    const reply = once(worker, "message");
                    worker.postMessage("check");
                    return reply;
                });
                for (let i = 0; i < 100; ++i) check(addon, retainedCounter);
                for (const [message] of await Promise.all(checked)) {
                    assert.strictEqual(message, "checked");
                }
                for (let i = 0; i < workers.length; ++i) {
                    const worker = workers[i];
                    if (i % 2 === 0) {
                        const exited = once(worker, "exit");
                        worker.postMessage("close");
                        assert.deepStrictEqual(await exited, [0]);
                    } else {
                        assert.strictEqual(await worker.terminate(), 1);
                    }
                    check(addon, retainedCounter);
                    check(reloaded, reloadedCounter);
                }
            } finally {
                await Promise.all(workers.map((worker) => worker.terminate()));
            }
        }
    }

    main().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
