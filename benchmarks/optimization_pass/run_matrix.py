#!/usr/bin/env python3
"""Run a recorded, prebuilt experiment matrix serially with rotating order.

The JSON config specifies a list of studies. Each has a name, format, reference,
and variants mapping names to {command: [...], env: {...}}. Build every variant
before invoking this runner. All observations, including regressions, are saved.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from summarize import observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cpu", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=5)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / "matrix.json").exists():
        parser.error("preserve the previous matrix and choose a fresh --out")
    manifest = dict(config=config, cpu=args.cpu, rounds=args.rounds, runs=[],
                    started_utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    manifest["runner_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    manifest["input_sha256"] = {}
    for study in config:
        for variant in study["variants"].values():
            for argument in variant["command"]:
                p = Path(argument)
                if p.is_file():
                    manifest["input_sha256"][str(p)] = hashlib.sha256(p.read_bytes()).hexdigest()
    if hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {args.cpu})
    samples = []
    for study in config:
        names = list(study["variants"])
        for round_index in range(args.rounds):
            shift = round_index % len(names)
            order = names[shift:] + names[:shift]
            if round_index % 2:
                order.reverse()
            for name in order:
                variant = study["variants"][name]
                env = os.environ.copy()
                env.update(variant.get("env", {}))
                print(study["name"], round_index + 1, name, flush=True)
                start = time.time()
                result = subprocess.run(variant["command"], env=env, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
                record = dict(study=study["name"], round=round_index + 1, variant=name,
                              start_epoch=start, stop_epoch=time.time(), returncode=result.returncode,
                              stdout=result.stdout, stderr=result.stderr)
                manifest["runs"].append(record)
                (out / "matrix.json").write_text(json.dumps(manifest, indent=2) + "\n")
                if result.returncode:
                    raise RuntimeError("failed matrix variant: " + str((study["name"], name)))
                if study["format"] == "json":
                    rows = [dict(case=r["name"], iterations=r.get("iterations", ""),
                                 ns_per_call=r["ns"], checksum=r.get("checksum", ""))
                            for r in map(json.loads, result.stdout.splitlines())]
                else:
                    rows = list(observations(study["format"], result.stdout))
                if not rows:
                    raise RuntimeError("no observations in " + study["name"])
                for row in rows:
                    row.update(study=study["name"], round=round_index + 1, variant=name)
                    samples.append(row)
    manifest["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    (out / "matrix.json").write_text(json.dumps(manifest, indent=2) + "\n")
    fields = ["study", "case", "round", "variant", "iterations", "ns_per_call", "checksum",
              "allocations_per_call", "allocated_bytes_per_call", "dynamic_copies_per_call"]
    with (out / "samples.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(samples)
    groups = {}
    for row in samples:
        group = groups.setdefault((row["study"], row["case"]), {})
        variant = group.setdefault(row["variant"], {})
        if row["round"] in variant:
            raise RuntimeError("duplicate observation")
        variant[row["round"]] = row
    summary = []
    for (study_name, case), variants in groups.items():
        study = next(s for s in config if s["name"] == study_name)
        if set(variants) != set(study["variants"]):
            raise RuntimeError("missing case in a variant: " + str((study_name, case)))
        reference = variants[study["reference"]]
        if len(reference) != args.rounds:
            raise RuntimeError("missing reference observations")
        reference_values = [float(reference[i]["ns_per_call"]) for i in sorted(reference)]
        reference_median = statistics.median(reference_values)
        for name, rows in variants.items():
            if sorted(rows) != sorted(reference):
                raise RuntimeError("incomplete variant")
            for i in rows:
                for field in ("checksum", "iterations"):
                    if str(rows[i].get(field, "")) != str(reference[i].get(field, "")):
                        raise RuntimeError("mismatched " + field + " in " + str((study_name, case, name)))
            values = [float(rows[i]["ns_per_call"]) for i in sorted(rows)]
            median = statistics.median(values)
            row = dict(study=study_name, case=case, variant=name, median=median,
                       minimum=min(values), maximum=max(values),
                       change_percent=(median / reference_median - 1) * 100 if reference_median else 0,
                       faster_rounds=sum(a < b for a, b in zip(values, reference_values)), rounds=args.rounds)
            summary.append(row)
            print(f'{study_name} {case} {name}: {median:.3f} ns '
                  f'({row["change_percent"]:+.1f}%; {row["faster_rounds"]}/{args.rounds})')
    with (out / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, list(summary[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary)


if __name__ == "__main__":
    main()
