#!/usr/bin/env python3
"""Compare fixed v30.2 output gains on identical untouched voice-sum renders.

This is an offline diagnostic, not a second app output path. The independent
tone processor retains the original fixed per-song drive and DC filter, then
applies each declared stateless curve. It never changes input files. Large
audio files belong in a temporary directory outside the source tree.
"""
import argparse
import concurrent.futures
import csv
import json
import math
from pathlib import Path
import subprocess

import numpy as np

from measure_loudness import CASES, meter


CURVES = [("g100", 1.0, 0.8), ("g150", 1.5, 0.8), ("g175", 1.75, 0.8),
          ("g200", 2.0, 0.8), ("g225", 2.25, 0.8)]
WIDER_KNEES = [("g100", 1.0, 0.8), ("g175k060", 1.75, 0.6),
               ("g175k040", 1.75, 0.4)]
WINDOWS = [("full", 0, 88), ("startup", 0, 10), ("settled", 40, 48)]


def distribution(values):
    return dict(min=min(values), median=float(np.median(values)), max=max(values))


def checked_pcm(path):
    expected = 44 + 88 * 48000 * 8
    if path.stat().st_size != expected:
        raise RuntimeError(f"Incomplete float WAV: {path}")
    pcm = np.memmap(path, dtype="<f4", mode="r", offset=44)
    if not np.all(np.isfinite(pcm)):
        raise RuntimeError(f"Nonfinite float PCM: {path}")
    return pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--processor", type=Path, required=True)
    parser.add_argument("--raw-audio", type=Path, required=True)
    parser.add_argument("--raw-metadata", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--wider-knees", action="store_true",
                        help="compare gentler K=0.60 and 0.40 at fixed G=1.75 against v30.1")
    args = parser.parse_args()
    curves = WIDER_KNEES if args.wider_knees else CURVES
    args.audio.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "ffmpeg-version.txt").write_text(
        subprocess.check_output(["ffmpeg", "-version"], text=True))

    def run_case(case):
        seed, intent, reason = case
        metadata = json.loads((args.raw_metadata / f"raw_{intent}_{seed}.json").read_text())
        source = args.raw_audio / f"raw_{intent}_{seed}.wav"
        checked_pcm(source)
        tone_path = args.audio / f"tone_{intent}_{seed}.wav"
        tone_command = [str(args.processor.resolve()), str(source.resolve()),
                        str(tone_path.resolve()), str(metadata["pattern_drive"])]
        tone_result = subprocess.run(tone_command, capture_output=True, text=True, check=True)
        tone = checked_pcm(tone_path)
        rows = []
        for label, gain, knee in curves:
            destination = args.audio / f"{label}_{intent}_{seed}.wav"
            command = [str(args.processor.resolve()), str(source.resolve()),
                       str(destination.resolve()), str(metadata["pattern_drive"]),
                       str(gain), str(knee), "1.0"]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            processing = json.loads(result.stdout)
            pcm = checked_pcm(destination)
            if float(np.max(np.abs(pcm))) > 1.0:
                raise RuntimeError(f"Output outside full scale: {destination}")
            candidate_rows = []
            for window, start, duration in WINDOWS:
                first = start * 48000 * 2
                last = (start + duration) * 48000 * 2
                segment = pcm[first:last]
                ideal = np.multiply(tone[first:last], np.float32(gain))
                error = segment.astype(np.float64) - ideal
                ideal_energy = float(np.mean(np.square(ideal, dtype=np.float64)))
                error_energy = float(np.mean(np.square(error)))
                residual = math.sqrt(error_energy / ideal_energy) if ideal_energy else 0.0
                levels = meter(destination, start, duration,
                               args.output / f"{label}_{intent}_{seed}_{window}.log")
                candidate_rows.append(dict(
                    curve=label, gain=gain, knee=knee, ceiling=1.0,
                    seed=seed, intent=intent, selection_reason=reason, window=window,
                    **levels,
                    knee_sample_fraction=float(np.mean(np.abs(ideal) > knee)),
                    above_099_fraction=float(np.mean(np.abs(segment) >= 0.99)),
                    fullscale_fraction=float(np.mean(np.abs(segment) >= 1.0)),
                    peak_curve_residual_relative_to_ideal_rms=residual,
                ))
            (args.output / f"{label}_{intent}_{seed}.json").write_text(json.dumps(dict(
                command=command, processing=processing, windows=candidate_rows,
                source_recipe=metadata["initial_recipe"]), indent=2) + "\n")
            rows.extend(candidate_rows)
            print(f"{label} {intent} {seed}: {candidate_rows[-1]['lufs']:.1f} LUFS; "
                  f"knee {100 * candidate_rows[-1]['knee_sample_fraction']:.3f} percent", flush=True)
        (args.output / f"tone_{intent}_{seed}.json").write_text(json.dumps(dict(
            command=tone_command, processing=json.loads(tone_result.stdout)), indent=2) + "\n")
        return rows

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        rows = [row for group in pool.map(run_case, CASES) for row in group]
    baseline = {(r["seed"], r["window"]): r for r in rows if r["curve"] == "g100"}
    for row in rows:
        old = baseline[(row["seed"], row["window"])]
        row["loudness_gain_lu"] = row["lufs"] - old["lufs"]
        row["rms_gain_db"] = row["rms_dbfs"] - old["rms_dbfs"]
    with (args.output / "calibration.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    summary = {}
    fields = ["lufs", "rms_dbfs", "true_peak_dbfs", "sample_peak_dbfs",
              "loudness_gain_lu", "rms_gain_db", "knee_sample_fraction",
              "above_099_fraction", "fullscale_fraction",
              "peak_curve_residual_relative_to_ideal_rms"]
    for label, gain, knee in curves:
        summary[label] = {window: {
            key: distribution([r[key] for r in rows if r["curve"] == label and r["window"] == window])
            for key in fields} for window, _, _ in WINDOWS}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({label: summary[label]["settled"] for label, _, _ in curves}, indent=2))


if __name__ == "__main__":
    main()
