#!/usr/bin/env python3
"""Verify exact v30 edited-recipe save/load and PCM replay using the public native API."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="retain executable and report here")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="technomatic-edited-"))
    output.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("C++17 compiler required")
    source = root / "app/src/main/cpp"
    executable = output / "edited-snapshots-v30"
    command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(source), str(root / "tools/edited_snapshot_v30.cpp"),
               str(source / "MusicEngine.cpp"), "-o", str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=60)
    report = result.stdout + "\n".join([
        "Host test compiles the production MusicEngine and exercises public load/save/render APIs.",
        "Canonical editrecipe=1 snapshots replay only present raw edit_* controls, once.",
        "Output summaries, including rounded lane gains and brightness, are not override inputs.",
        "The 40 supported controls are tested independently, together and across three reload generations.",
        "Comparison is byte-exact float PCM at 48 kHz within this host build; no cross-version promise is made.",
        "Android backend timing, device playback and perceived musical quality are separate checks.",
    ]) + "\n"
    (output / "edited-snapshot-test-report.txt").write_text(report, encoding="ascii")
    print(report, end="")
    print("Report: " + str(output / "edited-snapshot-test-report.txt"))


if __name__ == "__main__":
    main()
