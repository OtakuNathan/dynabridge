#!/usr/bin/env python3
"""Summarize every recorded sample; verify paired cases and checksums.

Change is the ratio of variant medians, not an application-level speedup.
Pairwise wins and complete sample ranges help expose VM/runtime variability.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import re
import statistics


def observations(family, output):
    if family in ("core", "core_threaded", "rpc"):
        for row in csv.DictReader(io.StringIO(output)):
            yield {
                "case": row.get("scenario", row.get("case")),
                "iterations": row["iterations"], "ns_per_call": row["ns_per_call"],
                "allocations_per_call": row.get("allocations_per_call", row.get("new_calls_per_call", "")),
                "dynamic_copies_per_call": row.get("dynamic_copies_per_call", ""),
                "allocated_bytes_per_call": row.get("allocated_bytes_per_call", ""),
                "checksum": row.get("checksum", ""),
            }
    elif family == "constructor":
        for line in output.splitlines():
            row = json.loads(line)
            yield {"case": row["name"], "iterations": row.get("iterations", 2000000),
                   "ns_per_call": row["ns"]}
    else:
        for line in output.splitlines():
            if family == "tcp":
                match = re.fullmatch(r"(.*?)\s+([0-9.]+) ns/call", line)
                if match:
                    yield {"case": match[1].strip(), "ns_per_call": match[2]}
            else:
                match = re.fullmatch(r"(.*?)\s+([0-9.]+)\s+[0-9]+\s+(-?[0-9]+)", line)
                if match:
                    yield {"case": match[1].strip(), "ns_per_call": match[2], "checksum": match[3]}


def write_csv(path, rows, fields):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    # Published evidence consolidates the original process logs without losing
    # their paths or content. Fresh local runs keep individual log files.
    archived = {}
    if (root / "raw_outputs.jsonl").exists():
        for line in (root / "raw_outputs.jsonl").read_text().splitlines():
            entry = json.loads(line)
            archived[entry["stdout_path"]] = entry["stdout"]
    samples = []
    for run in manifest["run_commands"]:
        if run["returncode"]:
            raise RuntimeError("failed probe in manifest: " + run["stdout"])
        path = root / run["stdout"]
        output = path.read_text() if path.exists() else archived[run["stdout"]]
        parsed = list(observations(run["family"], output))
        if not parsed:
            raise RuntimeError("no observations parsed: " + run["stdout"])
        for row in parsed:
            row.update(family=run["family"], pair=run["pair"], variant=run["variant"],
                       source=run["stdout"])
            samples.append(row)
    fields = ["family", "case", "pair", "variant", "iterations", "ns_per_call",
              "allocations_per_call", "allocated_bytes_per_call", "dynamic_copies_per_call",
              "checksum", "source"]
    write_csv(root / "samples.csv", samples, fields)
    groups = {}
    for row in samples:
        key = (row["family"], row["case"])
        group = groups.setdefault(key, {"before": {}, "after": {}})
        variant = group[row["variant"]]
        if row["pair"] in variant:
            raise RuntimeError("duplicate case in a process: " + str(key))
        variant[row["pair"]] = row
    summary = []
    for (family, case), variants in groups.items():
        pairs = sorted(variants["before"])
        if pairs != sorted(variants["after"]) or len(pairs) != manifest["pairs"]:
            raise RuntimeError("incomplete matched case: " + str((family, case)))
        for pair in pairs:
            for field in ("checksum", "iterations"):
                if str(variants["before"][pair].get(field, "")) != str(variants["after"][pair].get(field, "")):
                    raise RuntimeError("paired " + field + " mismatch: " + str((family, case, pair)))
        before = [float(variants["before"][p]["ns_per_call"]) for p in pairs]
        after = [float(variants["after"][p]["ns_per_call"]) for p in pairs]
        bm, am = statistics.median(before), statistics.median(after)
        entry = dict(family=family, case=case, before_median=bm, after_median=am,
                     change_percent=(am / bm - 1) * 100 if bm else "",
                     before_min=min(before), before_max=max(before),
                     after_min=min(after), after_max=max(after),
                     faster_pairs=sum(a < b for a, b in zip(after, before)), pairs=len(pairs))
        for field in ("allocations_per_call", "allocated_bytes_per_call", "dynamic_copies_per_call"):
            for variant in ("before", "after"):
                values = {str(variants[variant][p].get(field, "")) for p in pairs}
                if len(values) != 1:
                    raise RuntimeError("mechanism count varies: " + str((family, case, variant, field)))
                entry[variant + "_" + field] = values.pop()
        summary.append(entry)
    if not summary:
        raise RuntimeError("empty summary")
    write_csv(root / "summary.csv", summary, list(summary[0]))
    for row in summary:
        print(f'{row["family"]:12s} {row["case"]:40s} '
              f'{row["before_median"]:10.3f} -> {row["after_median"]:10.3f} ns '
              f'{row["change_percent"]:+7.1f}%  pairs {row["faster_pairs"]}/{row["pairs"]}')


if __name__ == "__main__":
    main()
