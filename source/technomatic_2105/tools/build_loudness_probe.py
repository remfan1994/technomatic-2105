#!/usr/bin/env python3
"""Build the same host float observer against a specified immutable engine.

--raw is explicitly an instrumented diagnostic: bypass the applyMaster call in
a copy, plus the archived v30 sidechain/transition multipliers when present.
It never modifies the supplied engine source or header.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--raw", action="store_true")
    args = parser.parse_args()
    native = args.engine_root.resolve() / "app/src/main/cpp"
    source = native / "MusicEngine.cpp"
    header = native / "MusicEngine.h"
    original = source.read_bytes()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.raw:
        text = original.decode()
        begin = "        mSidechain += (1.0f - mSidechain) * 0.0016f;"
        end = "        applyMaster(left, right);"
        assert text.count(end) == 1, "expected exactly one render master call"
        first = text.index(begin) if begin in text else text.index(end)
        last = text.index(end, first) + len(end)
        removed = text[first:last]
        if begin in text:
            assert "left *= duck;" in removed and "left *= mTransitionGain;" in removed
        text = text[:first] + "        // Host diagnostic: untouched voice sum before all whole-mix processing.\n" + text[last:]
        source = args.output.with_suffix(".raw.cpp")
        source.write_text(text)
        args.output.with_suffix(".raw-bypass.txt").write_text(removed + "\n")
    command = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               f'-DTECHNOMATIC_ENGINE_HEADER="{header}"', "-I", str(native),
               str(Path(__file__).resolve().with_name("loudness_probe.cpp")), str(source),
               "-o", str(args.output.resolve())]
    subprocess.run(command, check=True)
    args.output.with_suffix(".build.json").write_text(json.dumps({
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "instrumented_raw_voice_sum": args.raw, "command": command}, indent=2) + "\n")


if __name__ == "__main__":
    main()
