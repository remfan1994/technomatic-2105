#!/usr/bin/env python3
"""Check paired level gain and independently reconstructed fixed-output PCM.

When supplied, expected PCM comes from a raw voice-sum observer plus the
separate static-tone/curve processor. An authorized phrase-amplitude revision
instead uses score/RNG identity and the independent fixed-output unit tests;
old mixed PCM is then deliberately not asserted to be unchanged.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

from measure_loudness import CASES


def recipe_fields(recipe):
    fields = {}
    for item in recipe.split(";"):
        key, _, value = item.partition("=")
        if key != "outputrev":
            fields[key] = value
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--current-audio", type=Path, required=True)
    parser.add_argument("--expected-audio", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--current-label", default="v301")
    parser.add_argument("--baseline-label", default="v30")
    parser.add_argument("--expected-label", default="g100")
    parser.add_argument("--knee", type=float, default=0.80)
    parser.add_argument("--amplitude-revision", action="store_true",
                        help="compare score/RNG identity; the requested removal of phrase volume macros may change amplitudes")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []; samples = 0; events = 0
    baseline_identity = {x["seed"]: x for x in json.loads((args.baseline / "identity.json").read_text())}
    failures = []
    for seed, intent, _ in CASES:
        baseline = json.loads((args.baseline / f"{args.baseline_label}_{intent}_{seed}.json").read_text())
        current = json.loads((args.current / f"{args.current_label}_{intent}_{seed}.json").read_text())
        recipe_equal = recipe_fields(baseline["initial_recipe"]) == recipe_fields(current["initial_recipe"])
        notes_equal = (baseline["scheduler_fnv1a"] == current["scheduler_fnv1a"] and
                       baseline["scheduled_events"] == current["scheduled_events"])
        score_equal = (baseline_identity[seed]["score_fnv1a"] == current["score_fnv1a"] and
                       baseline_identity[seed]["scheduled_events"] == current["scheduled_events"])
        wanted = 44 + 88 * 48000 * 8
        actual_path = args.current_audio / f"{args.current_label}_{intent}_{seed}.wav"
        if actual_path.stat().st_size != wanted:
            raise RuntimeError(f"Incomplete PCM for {seed}")
        actual = np.memmap(actual_path, dtype="<f4", mode="r", offset=44)
        different = 0; maximum = 0.0; compared = 0
        if args.expected_audio:
            expected_path = args.expected_audio / f"{args.expected_label}_{intent}_{seed}.wav"
            if expected_path.stat().st_size != wanted:
                raise RuntimeError(f"Incomplete expected PCM for {seed}")
            expected = np.memmap(expected_path, dtype="<f4", mode="r", offset=44)
            difference = np.abs(expected - actual)
            different = int(np.count_nonzero(difference))
            maximum = float(np.max(difference)); compared = len(actual)
        samples += compared; events += current["scheduled_events"]
        rows.append(dict(seed=seed, intent=intent, recipe_equal=recipe_equal, full_events_equal=notes_equal,
                         score_and_rng_equal=score_equal,
                         compared_samples=compared, different_samples=different, max_abs_difference=maximum,
                         samples_in_peak_curve=int(np.count_nonzero(np.abs(actual) > args.knee)),
                         peak_curve_fraction=float(np.count_nonzero(np.abs(actual) > args.knee)) / len(actual)))
        identity_equal = score_equal if args.amplitude_revision else notes_equal
        if not recipe_equal or not identity_equal or different:
            failures.append(f"seed {seed}: recipe_equal={recipe_equal}, full_events_equal={notes_equal}, "
                            f"score_equal={score_equal}, PCM differences={different}")
    with (args.output / "identity.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
    baseline_rows = {(x["seed"], x["window"]): x for x in csv.DictReader((args.baseline / "measurements.csv").open())}
    paired = []
    for current in csv.DictReader((args.current / "measurements.csv").open()):
        old = baseline_rows[(current["seed"], current["window"])]
        row = {key: current[key] for key in ["seed", "intent", "window", "bpm"]}
        for key in ["lufs", "rms_dbfs", "sample_peak_dbfs", "true_peak_dbfs"]:
            row["before_" + key] = float(old[key]); row["after_" + key] = float(current[key])
            row["change_" + key] = float(current[key]) - float(old[key])
        row["after_fullscale_samples"] = int(current["fullscale_samples"])
        paired.append(row)
        if int(current["nonfinite_samples"]) or float(current["sample_peak"]) > 1:
            failures.append(f"Invalid final sample for {current['seed']} {current['window']}")
    with (args.output / "paired_levels.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=paired[0].keys()); writer.writeheader(); writer.writerows(paired)
    summary = dict(seeds=len(rows), scheduled_events=events, float_samples_compared=samples,
                   different_samples=sum(x["different_samples"] for x in rows) if args.expected_audio else None,
                   legacy_mixed_pcm_identity_asserted=False,
                   scheduled_amplitude_identity_asserted=not args.amplitude_revision,
                   independent_output_reconstruction_asserted=args.expected_audio is not None,
                   score_and_rng_equal_seeds=sum(x["score_and_rng_equal"] for x in rows),
                   amplitude_changed_seeds=sum(not x["full_events_equal"] for x in rows),
                   peak_curve_fraction=dict(min=min(x["peak_curve_fraction"] for x in rows),
                                            median=float(np.median([x["peak_curve_fraction"] for x in rows])),
                                            max=max(x["peak_curve_fraction"] for x in rows)), failures=failures)
    for window in ["startup", "settled", "full"]:
        group = [x for x in paired if x["window"] == window]
        summary[window] = {key: dict(min=min(x[key] for x in group), median=float(np.median([x[key] for x in group])),
                                     max=max(x[key] for x in group))
                           for key in ["change_lufs", "change_rms_dbfs", "after_lufs", "after_true_peak_dbfs"]}
    (args.output / "comparison_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
