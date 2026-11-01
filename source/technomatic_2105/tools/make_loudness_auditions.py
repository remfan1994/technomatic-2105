#!/usr/bin/env python3
"""Make level-preserving A/B FLAC excerpts and one 128kb/s Opus codec check.

Host libopus is not Android MediaCodec. FLAC is verified byte-for-byte against
the exact supplied 48kHz stereo PCM16; no normalization/gain/fades are added.
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

import numpy as np


def encoded_meter(path, log):
    result = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path),
                             "-af", "ebur128=peak=true:framelog=verbose", "-f", "null", "-"],
                            capture_output=True, text=True, check=True)
    log.write_text(result.stderr)
    summary = result.stderr.rsplit("Summary:", 1)[-1]
    lufs = float(re.search(r"\bI:\s+(-?[\d.]+)\s+LUFS", summary).group(1))
    true_peak = float(re.search(r"\bPeak:\s+(-?(?:[\d.]+|inf))\s+dBFS", summary).group(1))
    decoded = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-c:a", "pcm_f32le", "-"])
    pcm = np.frombuffer(decoded, dtype="<f4")
    return dict(lufs=lufs, true_peak_dbfs=true_peak, decoded_sample_peak=float(np.max(np.abs(pcm))),
                decoded_samples=int(pcm.size), decoded_overfull_samples=int(np.count_nonzero(np.abs(pcm) > 1.0)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-audio", type=Path, required=True)
    parser.add_argument("--after-audio", type=Path, required=True)
    parser.add_argument("--before-metadata", type=Path, required=True)
    parser.add_argument("--after-metadata", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--before-label", default="v30")
    parser.add_argument("--after-label", default="v301")
    parser.add_argument("--before-version", default="v30")
    parser.add_argument("--after-version", default="v30.1")
    args = parser.parse_args()
    for p in [args.output, args.evidence, args.work]: p.mkdir(parents=True, exist_ok=True)
    cases = [(1013904226, "slow"), (2654435761, "groove"),
             (3711502885, "driving_quiet_case"), (3668339987, "intense")]

    def make(job):
        number, seed, name, version = job
        intent = name.split("_")[0]
        label = args.before_label if version == "before" else args.after_label
        release = args.before_version if version == "before" else args.after_version
        folder = args.before_audio if version == "before" else args.after_audio
        metadata_folder = args.before_metadata if version == "before" else args.after_metadata
        metadata = json.loads((metadata_folder / f"{label}_{intent}_{seed}.json").read_text())
        source = folder / f"{label}_{intent}_{seed}.wav"
        if source.stat().st_size != 44 + 88 * 48000 * 8:
            raise RuntimeError(f"Incomplete source WAV: {source}")
        pcm = np.memmap(source, dtype="<f4", mode="r", offset=44).reshape(-1, 2)[40 * 48000:88 * 48000]
        # Match native export's lrint(float*32767), not an unrelated mastering
        # scale. Both versions use this same explicit conversion.
        pcm16 = np.rint(np.clip(pcm, -1, 1) * np.float32(32767)).astype("<i2").tobytes()
        release_slug = re.sub(r"[^A-Za-z0-9_-]", "_", release)
        stem = f"{number:02d}_{name}_{seed}_{'A' if version == 'before' else 'B'}_{release_slug}"
        raw = args.work / f"{stem}.pcm"; raw.write_bytes(pcm16)
        flac = args.output / f"{stem}.flac"
        comments = f"Seed {seed}; {intent}; host live excerpt [40,88) seconds; original signal level; no normalization"
        command = ["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", str(raw),
                   "-c:a", "flac", "-sample_fmt", "s16", "-compression_level", "8", "-metadata",
                   f"title=TECHNOMATIC 2105 {release} {name} seed {seed}", "-metadata", f"comment={comments}", str(flac)]
        subprocess.run(command, check=True)
        decoded = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(flac), "-f", "s16le", "-c:a", "pcm_s16le", "-"])
        if decoded != pcm16: raise RuntimeError(f"FLAC PCM changed: {flac}")
        info = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_format", "-show_streams", "-of", "json", str(flac)], text=True))
        tags = dict(info.get("format", {}).get("tags", {}))
        for stream in info["streams"]: tags.update(stream.get("tags", {}))
        if any("gain" in key.lower() for key in tags): raise RuntimeError(f"Unexpected gain tag: {flac}")
        metrics = encoded_meter(flac, args.evidence / f"{stem}_flac.log")
        entry = dict(file=flac.name, seed=seed, intent=intent, version=version, release=release, bpm=metadata["bpm"],
                     start_seconds=40, end_seconds=88, format="FLAC lossless PCM16 stereo 48000Hz",
                     sha256=hashlib.sha256(flac.read_bytes()).hexdigest(), pcm16_sha256=hashlib.sha256(pcm16).hexdigest(),
                     flac_pcm_exact=True, gain_tags_present=False, recipe=metadata["initial_recipe"], **metrics)
        codec = None
        if version == "after":
            opus = args.work / f"{stem}_128k.opus"
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", "48000", "-ac", "2", "-i", str(raw),
                            "-c:a", "libopus", "-b:a", "128000", "-frame_duration", "20", "-application", "audio", str(opus)], check=True)
            data = opus.read_bytes(); head = data.index(b"OpusHead")
            gain = struct.unpack_from("<h", data, head + 16)[0]
            if gain != 0: raise RuntimeError("Opus output gain is not zero")
            codec = dict(seed=seed, intent=intent, codec="host libopus", bitrate=128000, frame_ms=20,
                         output_gain_q78_db=gain, **encoded_meter(opus, args.evidence / f"{stem}_opus.log"))
        return entry, codec

    jobs = [(i + 1, seed, name, version) for i, (seed, name) in enumerate(cases) for version in ["before", "after"]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        result = list(pool.map(make, jobs))
    entries = [x[0] for x in result]; codecs = [x[1] for x in result if x[1]]
    manifest = dict(description="Level-preserving matched host-render auditions", samples=entries,
                    caveats=["Not Android MediaCodec or a device/headphone recording.",
                             "No loudness normalization, ReplayGain, automatic volume envelope or added fades.",
                             "All excerpts are exact [40,88) second cuts of live rendering from time zero.",
                             "The finite Export arrangement can make its own musical ending and is tested separately."])
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.evidence / "audition_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    with (args.evidence / "codec_measurements.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=codecs[0].keys()); writer.writeheader(); writer.writerows(codecs)
    lines = [f"TECHNOMATIC 2105 {args.before_version} / {args.after_version}: fixed-level A/B auditions", "", 
             "Each pair uses the identical seed and exact [40,88) second excerpt (48 seconds).",
             f"A is delivered {args.before_version}; B is the final {args.after_version} output.",
             "Keep player/system volume unchanged between A and B. Player normalization, if enabled,",
             "can hide the level difference; these files contain no gain/ReplayGain tags.", "",
             "All eight files are 48kHz stereo PCM16 stored losslessly in FLAC. Decoded PCM was",
             "verified byte-for-byte. No mastering normalization or fade was added to either side.",
             "These are host renders, not recordings of Android playback or the Android encoder.",
             "The phrase arrangement and individual note envelopes still create musical gaps/dynamics.", "",
             "The driving pair uses the quietest settled-loudness seed in the declared 16-seed sample.",
             "The manifest records seeds, BPM, recipes, hashes, exact boundaries and measured levels.", ""]
    for entry in entries:
        lines.append(f"{entry['file']}: {entry['bpm']:.2f} BPM; {entry['lufs']:.1f} LUFS; {entry['true_peak_dbfs']:+.1f} dBTP")
    lines += ["", "Digital full scale is permitted. Interpolated/codec-decoded peaks can exceed 0dBFS;",
              "these files are not claimed to be universally free of reconstructed-peak overshoot.", ""]
    (args.output / "README.txt").write_text("\n".join(lines), encoding="ascii")
    print(json.dumps(dict(flac_files=len(entries), flac_pcm_exact=all(x["flac_pcm_exact"] for x in entries),
                          codecs=codecs), indent=2))


if __name__ == "__main__":
    main()
