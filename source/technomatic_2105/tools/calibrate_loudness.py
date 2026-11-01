#!/usr/bin/env python3
"""Compare stateless output curves offline on the frozen raw voice-sum sample."""
import argparse
import concurrent.futures
import csv
import json
from pathlib import Path
import subprocess

import numpy as np

from measure_loudness import CASES, meter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--processor", type=Path, required=True)
    parser.add_argument("--raw-audio", type=Path, required=True)
    parser.add_argument("--raw-metadata", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()
    args.audio.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    # The unbounded static-tone row is an intermediate measurement, not a
    # proposed final output. Curves below share one fixed gain across all seeds.
    curves = [("tone", []), ("g100", [1, .80, 1.0]),
              ("g125", [1.25, .80, 1.0])]

    def run(job):
        (seed, intent, reason), (name, curve) = job
        metadata = json.loads((args.raw_metadata / f"raw_{intent}_{seed}.json").read_text())
        source = args.raw_audio / f"raw_{intent}_{seed}.wav"
        dest = args.audio / f"{name}_{intent}_{seed}.wav"
        command = [str(args.processor.resolve()), str(source.resolve()), str(dest.resolve()),
                   str(metadata["pattern_drive"]), *map(str, curve)]
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        processing = json.loads(result.stdout)
        measurement = meter(dest, 40, 48, args.output / f"{name}_{intent}_{seed}.log")
        row = dict(curve=name, seed=seed, intent=intent, **measurement,
                   knee_sample_fraction=processing["knee_samples"] / processing["samples"],
                   whole_render_rms=processing["rms"], whole_render_peak=processing["peak"])
        (args.output / f"{name}_{intent}_{seed}.json").write_text(json.dumps(
            dict(command=command, processing=processing, measurement=row), indent=2) + "\n")
        return row

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        rows = list(pool.map(run, [(case, curve) for curve in curves for case in CASES]))
    with (args.output / "calibration.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
    summary = {}
    for name, _ in curves:
        group = [row for row in rows if row["curve"] == name]
        summary[name] = {key: dict(min=min(row[key] for row in group),
                                  median=float(np.median([row[key] for row in group])),
                                  max=max(row[key] for row in group))
                         for key in ["lufs", "rms_dbfs", "true_peak_dbfs", "sample_peak_dbfs", "knee_sample_fraction"]}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
