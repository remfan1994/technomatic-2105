#!/usr/bin/env python3
"""Check fixed-output calibration candidates through the same host Opus path.

This is not an Android MediaCodec or hardware test. Input excerpts remain at
their generated level; conversion uses the app's float-to-PCM16 expression.
"""
import argparse
import concurrent.futures
import csv
import json
from pathlib import Path
import re
import struct
import subprocess

import numpy as np

from make_loudness_auditions import encoded_meter


CASES = [(1013904226, "slow"), (2654435761, "groove"),
         (3711502885, "driving"), (3668339987, "intense")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--labels", nargs="+", required=True)
    args = parser.parse_args()
    if any(not re.fullmatch(r"[A-Za-z0-9_-]+", x) for x in args.labels):
        parser.error("labels must contain only letters, digits, hyphen or underscore")
    args.output.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)

    def run(job):
        label, (seed, intent) = job
        stem = f"{label}_{intent}_{seed}"
        source = args.audio / f"{stem}.wav"
        if source.stat().st_size != 44 + 88 * 48000 * 8:
            raise RuntimeError(f"Incomplete source WAV: {source}")
        pcm = np.memmap(source, dtype="<f4", mode="r", offset=44)[40 * 48000 * 2:88 * 48000 * 2]
        if not np.all(np.isfinite(pcm)) or float(np.max(np.abs(pcm))) > 1:
            raise RuntimeError(f"Source PCM is not bounded and finite: {source}")
        raw = args.work / f"{stem}.pcm"
        raw.write_bytes(np.rint(pcm * np.float32(32767)).astype("<i2").tobytes())
        opus = args.work / f"{stem}.opus"
        command = ["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", str(raw),
                   "-c:a", "libopus", "-b:a", "128000", "-frame_duration", "20", "-application", "audio", str(opus)]
        subprocess.run(command, check=True)
        data = opus.read_bytes()
        gain = struct.unpack_from("<h", data, data.index(b"OpusHead") + 16)[0]
        if gain != 0:
            raise RuntimeError("Opus output gain is not zero")
        metrics = encoded_meter(opus, args.output / f"{stem}.log")
        decoded = np.frombuffer(subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(opus), "-f", "f32le", "-c:a", "pcm_f32le", "-"
        ]), dtype="<f4")
        error = decoded.astype(np.float64) - np.clip(decoded, -1, 1)
        signal_energy = float(np.mean(np.square(decoded, dtype=np.float64)))
        error_energy = float(np.mean(np.square(error)))
        row = dict(curve=label, seed=seed, intent=intent, bitrate=128000, frame_ms=20,
                   output_gain_q78_db=gain, **metrics,
                   decoded_overfull_fraction=metrics["decoded_overfull_samples"] / metrics["decoded_samples"],
                   decoded_integer_clip_error_relative_rms=(error_energy / signal_energy) ** 0.5)
        (args.output / f"{stem}.json").write_text(json.dumps(dict(command=command, result=row), indent=2) + "\n")
        return row

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows = list(pool.map(run, [(label, case) for label in args.labels for case in CASES]))
    with (args.output / "codec_measurements.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
