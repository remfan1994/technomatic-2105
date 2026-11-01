#!/usr/bin/env python3
"""Render a fixed multi-intent sample and meter untouched float PCM with FFmpeg.

The twelve sparse candidates were chosen before loudness metering from the
existing v30 primary-note-density audit. They are not assumed to be the twelve
quietest possible seeds. Audio stays outside the source/evidence directory.
"""
import argparse
import concurrent.futures
import csv
import json
import math
from pathlib import Path
import re
import struct
import subprocess

import numpy as np

CASES = [
    (1013904226, "slow", "first intent representative"),
    (2654435761, "groove", "first intent representative"),
    (2027808452, "driving", "first intent representative"),
    (3668339987, "intense", "first intent representative"),
    (1866542167, "slow", "sparse primary candidate"),
    (3267723310, "slow", "sparse primary candidate"),
    (1775118300, "slow", "sparse primary candidate"),
    (2563011894, "groove", "sparse primary candidate"),
    (3394068386, "groove", "sparse primary candidate"),
    (543445131, "groove", "sparse primary candidate"),
    (2528090685, "driving", "sparse primary candidate"),
    (2345242951, "driving", "sparse primary candidate"),
    (3711502885, "driving", "sparse primary candidate"),
    (169507974, "intense", "sparse primary candidate"),
    (2175734977, "intense", "sparse primary candidate"),
    (3824508201, "intense", "sparse primary candidate"),
]


def decibels(x):
    return 20 * math.log10(x) if x > 0 else -math.inf


def meter(path, start, duration, log_path):
    with path.open("rb") as f:
        header = f.read(44)
    if len(header) != 44 or header[36:40] != b"data":
        raise RuntimeError(f"Unsupported float WAV header: {path}")
    declared_bytes = struct.unpack_from("<I", header, 40)[0]
    if path.stat().st_size != 44 + declared_bytes or declared_bytes < round((start + duration) * 48000) * 8:
        raise RuntimeError(f"Incomplete float WAV: {path}")
    command = ["ffmpeg", "-hide_banner", "-nostats", "-ss", str(start), "-t", str(duration),
               "-i", str(path), "-af", "ebur128=peak=true:framelog=verbose", "-f", "null", "-"]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    log_path.write_text("COMMAND " + " ".join(command) + "\n" + result.stderr)
    summary = result.stderr.rsplit("Summary:", 1)[-1]
    values = {}
    for key, expression in [("lufs", r"\bI:\s+(-?[\d.]+)\s+LUFS"),
                            ("lra_lu", r"\bLRA:\s+(-?[\d.]+)\s+LU"),
                            ("true_peak_dbfs", r"\bPeak:\s+(-?(?:[\d.]+|inf))\s+dBFS")]:
        match = re.search(expression, summary)
        if not match:
            raise RuntimeError(f"Missing {key} in {log_path}")
        values[key] = float(match.group(1))
    pcm = np.memmap(path, dtype="<f4", mode="r", offset=44).reshape(-1, 2)
    segment = pcm[round(start * 48000): round((start + duration) * 48000)]
    rms = math.sqrt(float(np.mean(np.square(segment, dtype=np.float64))))
    peak = float(np.max(np.abs(segment)))
    values.update(rms=rms, rms_dbfs=decibels(rms), sample_peak=peak, sample_peak_dbfs=decibels(peak),
                  nonfinite_samples=int(np.count_nonzero(~np.isfinite(segment))),
                  fullscale_samples=int(np.count_nonzero(np.abs(segment) >= 1)))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--reuse", action="store_true")
    args = parser.parse_args()
    args.audio.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "ffmpeg-version.txt").write_text(subprocess.check_output(["ffmpeg", "-version"], text=True))

    def run_case(case):
        seed, intent, reason = case
        stem = f"{args.label}_{intent}_{seed}"
        audio = args.audio / f"{stem}.wav"
        metadata = args.output / f"{stem}.json"
        if not (args.reuse and audio.exists() and metadata.exists() and audio.stat().st_size == 44 + 88 * 48000 * 8):
            temporary = audio.with_suffix(".partial.wav")
            command = [str(args.renderer.resolve()), str(temporary.resolve()), str(seed), "88"]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            data = json.loads(result.stdout)
            if temporary.stat().st_size != 44 + data["frames"] * 8:
                raise RuntimeError(f"Incomplete render: {temporary}")
            temporary.replace(audio)
            temporary.with_name(temporary.name + ".levels.csv").replace(audio.with_name(audio.name + ".levels.csv"))
            data["render_command"] = command
            data["selection_reason"] = reason
            metadata.write_text(json.dumps(data, indent=2) + "\n")
        else:
            data = json.loads(metadata.read_text())
        expected_intent = ["slow", "groove", "driving", "intense"].index(intent)
        if data["intent"] != expected_intent:
            raise RuntimeError(f"Intent changed for {seed}: {data['intent']}")
        rows = []
        for window, start, duration in [("full", 0, 88), ("startup", 0, 10), ("settled", 40, 48)]:
            values = meter(audio, start, duration, args.output / f"{stem}_{window}.log")
            rows.append(dict(label=args.label, seed=seed, intent=intent, selection_reason=reason,
                             bpm=data["bpm"], window=window, start_seconds=start, duration_seconds=duration,
                             **values, scheduler_fnv1a=data["scheduler_fnv1a"],
                             scheduled_events=data["scheduled_events"], pcm_fnv1a=data["pcm_fnv1a"]))
        print(f"{args.label} {intent} {seed}: settled {rows[-1]['lufs']:.1f} LUFS, "
              f"{rows[-1]['true_peak_dbfs']:.1f} dBTP, RMS {rows[-1]['rms_dbfs']:.2f} dBFS", flush=True)
        return rows

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        rows = [row for case_rows in pool.map(run_case, CASES) for row in case_rows]
    with (args.output / "measurements.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    summary = {}
    for window in ["full", "startup", "settled"]:
        window_rows = [row for row in rows if row["window"] == window]
        summary[window] = {key: {"min": min(row[key] for row in window_rows),
                                 "median": float(np.median([row[key] for row in window_rows])),
                                 "max": max(row[key] for row in window_rows)}
                           for key in ["lufs", "rms_dbfs", "sample_peak_dbfs", "true_peak_dbfs"]}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    identity_keys = ["seed", "scheduler_fnv1a", "score_fnv1a", "amplitude_fnv1a", "scheduled_events"]
    identities = [json.loads((args.output / f"{args.label}_{intent}_{seed}.json").read_text())
                  for seed, intent, _ in CASES]
    if all(all(key in row for key in identity_keys) for row in identities):
        (args.output / "identity.json").write_text(json.dumps(
            [{key: row[key] for key in identity_keys} for row in identities], indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
