#!/usr/bin/env python3
"""Encode the documented host-render auditions; never uses Android MediaCodec."""
import argparse
import array
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import wave
import zipfile

INTENTS = {0: "Slow", 1: "Unhurried groove", 2: "Driving", 3: "Intense"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", default="/tmp/technomatic_v30_previews")
    parser.add_argument("--zip", default="/tmp/technomatic_2105_v30_auditions.zip")
    args = parser.parse_args()
    directory = Path(args.directory)
    project = Path(__file__).resolve().parents[1]
    tables = {}
    for version in (29, 30):
        with (project / "tools" / "validation" / f"v{version}_primary_development.csv").open() as f:
            tables[version] = {int(row["seed"]): row for row in csv.DictReader(f)}
    rows = []
    for wav in sorted(directory.glob("*.wav")):
        version = int(wav.stem.split("_")[0][1:])
        seed = int(wav.stem.split("_")[-1])
        info = tables[version][seed]
        intent = int(info["intent"])
        start = 128 if "comparison" in wav.stem else 40
        with wave.open(str(wav), "rb") as source:
            assert source.getframerate() == 48000 and source.getnchannels() == 2
            assert source.getsampwidth() == 2 and source.getnframes() == 48 * 48000
            pcm = array.array("h", source.readframes(source.getnframes()))
        peak = max(abs(x) for x in pcm) / 32768
        rms = math.sqrt(sum(x*x for x in pcm) / len(pcm)) / 32768
        clipping = sum(x == -32768 or x == 32767 for x in pcm)
        assert peak < 1 and clipping == 0 and rms > 0
        channels = "Auto; channels retired" if version == 30 else "No Channel (v29 baseline)"
        tags = {
            "TITLE": f"TECHNOMATIC 2105 v{version} - {INTENTS[intent]} - seed {seed}",
            "ARTIST": "TECHNOMATIC 2105",
            "SEED": str(seed), "CHANNELS": channels, "BPM": info["bpm"],
            "MUSICAL_INTENT": INTENTS[intent], "ENGINE_INTENT": str(intent),
            "ENGINE_VERSION": str(version), "EXCERPT_START_SECONDS": str(start),
            "EXCERPT_END_SECONDS": str(start + 48),
            "COMMENT": f"Seed {seed}; {channels}; {INTENTS[intent]}; host native live-engine render, {start}-{start+48}s. Opus encoded with ffmpeg; not an Android export.",
        }
        opus = wav.with_suffix(".opus")
        command = ["ffmpeg", "-v", "error", "-y", "-i", str(wav), "-c:a", "libopus", "-b:a", "160k", "-vbr", "on"]
        for key, value in tags.items():
            command += ["-metadata", f"{key}={value}"]
        subprocess.run(command + [str(opus)], check=True)
        probe = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(opus)]))
        stream = probe["streams"][0]
        actual_tags = {k.upper(): v for k, v in stream.get("tags", {}).items()}
        assert actual_tags["SEED"] == str(seed) and actual_tags["ENGINE_VERSION"] == str(version)
        decoded = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(opus), "-f", "s16le", "-acodec", "pcm_s16le", "-"])
        assert len(decoded) == 48 * 48000 * 2 * 2
        rows.append({"file": opus.name, "version": version, "seed": seed, "intent": INTENTS[intent],
                     "bpm": float(info["bpm"]), "start": start, "end": start+48,
                     "wav_peak": peak, "wav_rms": rms, "clipped_samples": clipping,
                     "decoded_frames": len(decoded)//4, "sha256": hashlib.sha256(opus.read_bytes()).hexdigest(),
                     "probe": probe})
    assert len(rows) == 7
    report = {"method": "48kHz stereo native live renders; original prefix rendered before excerpt; ffmpeg libopus 160k VBR", "files": rows}
    (directory / "probe.json").write_text(json.dumps(report, indent=2) + "\n")
    descriptions = [
        "TECHNOMATIC 2105 v30 - 48-second host-render auditions", "",
        "The three v30 listening-intent examples are the first slow, unhurried-groove and intense seeds in the deterministic test sequence i*2654435761 modulo 2^32. Their live excerpts cover seconds 40 through 88.",
        "The two before/after pairs use seeds 3041712678 and 2514750925, selected by the independent v29 primary-melody audit as low-variety or high-recurrence examples. Both versions cover the identical live interval 128 through 176 seconds. No specific failing seed or recording was supplied by the user, so these are test cases, not reconstructions of that unprovided recording.",
        "The engine renders the complete prefix before capturing each excerpt. These are live-generation excerpts without a finite-export ending. v29 uses No Channel; v30 automatically composes with channels retired. No loudness normalization was applied.",
        "All seven WAV sources contain exactly 2304000 stereo frames at 48kHz (48.000s), no clipped PCM samples, and nonzero PCM energy. Every Opus decodes to the same exact frame count. Container duration can include Opus preroll; decoded sample count is checked explicitly.",
        "Opus encoding: ffmpeg/libopus, requested 160kbit/s VBR. These are host native renders, not validation of the Android MediaCodec export path. They have not been judged by subjective listening. Inspectable tags include seed, channel status, engine version, BPM, intent and excerpt range. Each .txt file contains the recipe used by that render.", "",
        "FILE | ENGINE | SEED | INTENT | BPM | LIVE RANGE | WAV PEAK | WAV RMS",
    ]
    for row in rows:
        descriptions.append(f"{row['file']} | v{row['version']} | {row['seed']} | {row['intent']} | {row['bpm']:.6f} | {row['start']}-{row['end']}s | {row['wav_peak']:.6f} | {row['wav_rms']:.6f}")
    descriptions += ["", "Native MusicEngine.cpp SHA256:",
        "v29 5a3a3c9f58209f2fee8235fd8925093bb9689c196ec06f4c95b7d7b1eba86658",
        "v30 759885caecfaaa12dc07e83977e94948f17d853b1a69284ab1dd7adfcfe58496", ""]
    # Plain ASCII makes this README convenient on Android and command-line tools.
    (directory / "README.txt").write_text("\n".join(descriptions), encoding="ascii")
    with zipfile.ZipFile(args.zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(directory.iterdir()):
            if path.suffix in (".opus", ".txt", ".json"):
                archive.write(path, path.name)
    print(json.dumps({"zip": args.zip, "files": len(rows), "bytes": Path(args.zip).stat().st_size,
                      "peak_max": max(r["wav_peak"] for r in rows)}, indent=2))


if __name__ == "__main__":
    main()
