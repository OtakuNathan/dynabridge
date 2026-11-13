#!/usr/bin/env python3
"""Build identical probes against two Portal trees, then measure serially.

Builds and measurements are deliberately separate phases. Both variants use
the benchmark sources next to this script, the same compiler/options, and the
same optional Flux Foundry dependency. Raw outputs, commands, source hashes,
binary hashes and process order are retained; no result is filtered as an outlier.
The runtime build helpers currently target Linux with GCC or Clang.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import sysconfig
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FAMILIES = ("core", "core_threaded", "rpc", "python", "node", "constructor", "tcp")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(command, **kwargs):
    return subprocess.run(command, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, default=ROOT)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ff-include", type=Path)
    parser.add_argument("--node-include", type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--families", default=",".join(FAMILIES))
    parser.add_argument("--pairs", type=int, default=7)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--iterations", type=int, default=1000000)
    parser.add_argument("--runtime-iterations", type=int, default=1000000)
    parser.add_argument("--node-iterations", type=int, default=100000)
    parser.add_argument("--tcp-iterations", type=int, default=20000)
    phase = parser.add_mutually_exclusive_group()
    phase.add_argument("--build-only", action="store_true")
    phase.add_argument("--run-only", action="store_true")
    args = parser.parse_args()
    families = args.families.split(",")
    if set(families) - set(FAMILIES) or not 1 <= args.pairs <= 30:
        parser.error("unknown family or pairs outside 1..30")
    if min(args.iterations, args.runtime_iterations, args.node_iterations,
           args.tcp_iterations) <= 0:
        parser.error("iteration counts must be positive")
    available = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else []
    cpu = args.cpu if args.cpu is not None else (available[0] if available else None)
    if available and cpu not in available:
        parser.error("CPU is outside the available affinity mask")
    out = args.out.resolve()
    binaries, logs = out / "binaries", out / "logs"
    binaries.mkdir(parents=True, exist_ok=True)
    logs.mkdir(exist_ok=True)
    manifest_path = out / "manifest.json"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++14", "-O3", "-DNDEBUG", "-pthread", "-Wall", "-Wextra"]
    flags += shlex.split(os.environ.get("EXTRA_CXXFLAGS", ""))
    roots = {"before": args.baseline.resolve(), "after": args.candidate.resolve()}
    manifest = {
        "platform": platform.platform(), "python": platform.python_version(),
        "compiler": capture(compiler + ["--version"], check=True).stdout,
        "flags": flags, "roots": {k: str(v) for k, v in roots.items()},
        "ff_include": str(args.ff_include.resolve()) if args.ff_include else None,
        "node_include": str(args.node_include.resolve()) if args.node_include else None,
        "build_commands": [], "binary_sha256": {}, "source_sha256": {},
    }
    if args.run_only:
        manifest = json.loads(manifest_path.read_text())
    else:
        if manifest_path.exists():
            parser.error("output already has a manifest; choose a fresh --out or --run-only")
        for variant, root in roots.items():
            manifest["source_sha256"][variant] = {
                str(p.relative_to(root)): sha(p) for p in sorted((root / "portal").rglob("*.h"))
            }
            manifest["source_sha256"][variant].update({
                str(p.relative_to(root)): sha(p) for p in sorted((root / "tests").rglob("*.def"))
            })
        manifest["source_sha256"]["harness"] = {
            str(p.relative_to(ROOT)): sha(p) for p in sorted((ROOT / "benchmarks").rglob("*"))
            if p.is_file() and p.suffix in (".cpp", ".js", ".py", ".def")
        }
        if args.ff_include:
            manifest["source_sha256"]["flux_foundry"] = {
                str(p.relative_to(args.ff_include)): sha(p)
                for folder in ("flow", "memory", "utility", "extension", "lockfree", "executor")
                for p in sorted((args.ff_include / folder).rglob("*.h"))
            }

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

    def build(name, source, variant, extra=(), libraries=()):
        target = binaries / (name + "-" + variant)
        command = compiler + flags + list(extra) + [
            "-I" + str(roots[variant]), "-I" + str(ROOT),
            str(source), "-o", str(target)] + list(libraries)
        print("build", target.name, flush=True)
        result = capture(command)
        (logs / ("build-" + target.name + ".log")).write_text(result.stdout + result.stderr)
        manifest["build_commands"].append({"command": command, "returncode": result.returncode})
        if result.returncode == 0:
            manifest["binary_sha256"][target.name] = sha(target)
        save()
        if result.returncode:
            raise RuntimeError("build failed: " + str(target) + "; see logs")

    if not args.run_only:
        for variant in roots:
            ff = ["-I" + str(args.ff_include.resolve())] if args.ff_include else []
            if {"core", "core_threaded"} & set(families):
                build("core", HERE / "core_probe.cpp", variant, ff)
            if "rpc" in families:
                build("rpc", HERE / "rpc_probe.cpp", variant)
            if "python" in families:
                # Relocated Python distributions can retain their original
                # build-time LIBDIR. Prefer an existing library, including the
                # active interpreter's prefix, over blindly passing that path.
                library_name = sysconfig.get_config_var("LDLIBRARY")
                library_dirs = [sysconfig.get_config_var("LIBDIR"),
                                str(Path(sys.base_prefix) / "lib"),
                                sysconfig.get_config_var("LIBPL")]
                library = next((Path(p) / library_name for p in library_dirs
                                if p and (Path(p) / library_name).is_file()), None)
                if library is None:
                    raise RuntimeError("cannot locate the active Python embedding library")
                python_libraries = [str(library), "-Wl,-rpath," + str(library.parent)]
                python_libraries += shlex.split(sysconfig.get_config_var("LIBS") or "")
                python_libraries += shlex.split(sysconfig.get_config_var("SYSLIBS") or "")
                build("python", ROOT / "benchmarks/python_call_benchmark.cpp", variant,
                      ["-I" + sysconfig.get_path("include")], python_libraries)
            if {"node", "constructor"} & set(families):
                if not args.node_include:
                    parser.error("Node families require --node-include")
                build("node.node", ROOT / "benchmarks/node_call_benchmark_addon.cpp", variant,
                      ["-fPIC", "-shared", "-I" + str(args.node_include.resolve())])
            if "tcp" in families:
                extra = ff + (["-DPORTAL_HAS_FLUX_FOUNDRY=1"] if ff else [])
                build("tcp", ROOT / "benchmarks/rpc_call_benchmark.cpp", variant, extra)
        manifest["runner_sha256"] = sha(Path(__file__))
        save()
    if args.build_only:
        return

    # Refuse silently changed executables between the separate build/run phases.
    for name, digest in manifest["binary_sha256"].items():
        if sha(binaries / name) != digest:
            raise RuntimeError("binary changed after build: " + name)
    if manifest.get("run_commands"):
        parser.error("output already contains a run; preserve it and choose a fresh directory")
    manifest.update(cpu=cpu, available_cpus=available, pairs=args.pairs,
                    iterations=args.iterations, runtime_iterations=args.runtime_iterations,
                    node_iterations=args.node_iterations, tcp_iterations=args.tcp_iterations,
                    run_commands=[], started_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    if {"node", "constructor"} & set(families):
        manifest["node_version"] = capture([args.node, "--version"], check=True).stdout.strip()
    # Child processes inherit this affinity. TCP worker threads intentionally
    # share it: these are loopback regression controls, not a network capacity test.
    if cpu is not None and hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {cpu})
    save()
    for family in families:
        for pair in range(args.pairs):
            order = ("before", "after") if pair % 2 == 0 else ("after", "before")
            for variant in order:
                env = os.environ.copy()
                if family == "core_threaded":
                    command = [str(binaries / ("core-" + variant)), str(args.iterations),
                               "--threaded-process"]
                elif family in ("core", "rpc"):
                    command = [str(binaries / (family + "-" + variant)), str(args.iterations)]
                elif family == "python":
                    env["PORTAL_BENCH_ITERS"] = str(args.runtime_iterations)
                    command = [str(binaries / ("python-" + variant))]
                elif family in ("node", "constructor"):
                    env["PORTAL_BENCH_ITERS"] = str(args.node_iterations)
                    script = "node_constructor_benchmark.js" if family == "constructor" else "node_call_benchmark.js"
                    # Node needs a .node suffix, even when the variant is recorded
                    # in the build filename. A byte-for-byte copy is explicit.
                    addon = binaries / ("addon-" + variant + ".node")
                    if not addon.exists():
                        addon.write_bytes((binaries / ("node.node-" + variant)).read_bytes())
                    if sha(addon) != manifest["binary_sha256"]["node.node-" + variant]:
                        raise RuntimeError("addon copy differs from the recorded binary")
                    command = [args.node, "--expose-gc", str(ROOT / "benchmarks" / script), str(addon)]
                else:
                    command = [str(binaries / ("tcp-" + variant)), str(args.tcp_iterations)]
                name = f"{family}-{pair + 1:02d}-{variant}"
                print("run", name, flush=True)
                start = time.time()
                result = capture(command, env=env, timeout=300)
                (logs / (name + ".stdout")).write_text(result.stdout)
                (logs / (name + ".stderr")).write_text(result.stderr)
                manifest["run_commands"].append({
                    "family": family, "pair": pair + 1, "variant": variant,
                    "command": command, "PORTAL_BENCH_ITERS": env.get("PORTAL_BENCH_ITERS"),
                    "start_epoch": start, "stop_epoch": time.time(),
                    "returncode": result.returncode,
                    "stdout": str(Path("logs") / (name + ".stdout")),
                    "stderr": str(Path("logs") / (name + ".stderr")),
                })
                save()
                if result.returncode:
                    raise RuntimeError("probe failed: " + name + "; no samples discarded")
    manifest["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    save()


if __name__ == "__main__":
    main()
