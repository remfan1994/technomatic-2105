#!/usr/bin/env python3
"""Build and run focused fixed-output tests against production native sources."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="technomatic-fixed-output-"))
    output.mkdir(parents=True, exist_ok=True)
    native = root / "app/src/main/cpp"
    source = (native / "MusicEngine.cpp").read_text() + (native / "MusicEngine.h").read_text()
    forbidden = ["mAgcRms", "mAgcGain", "mMaster", "mSidechain", "mTransitionGain", "tailGain", "mOutputStage"]
    found = [name for name in forbidden if re.search(r"\b" + name + r"\b", source)]
    if found:
        raise SystemExit("FAIL retired whole-mix state remains: " + ", ".join(found))
    binary = output / "fixed-output-tests"
    command = ["g++", "-std=c++17", "-O1" if args.sanitize else "-O2", "-Wall", "-Wextra", "-Werror",
               str(root / "tools/fixed_output_tests.cpp"), str(native / "MusicEngine.cpp"), "-o", str(binary)]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary), str(output)], capture_output=True, text=True, timeout=180)
    report = "PASS retired AGC, master attenuation, ducking and fade state absent from native source\n" + result.stdout + result.stderr
    report += "Host result only; this does not measure an Android device, headphone output or subjective listening quality.\n"
    (output / "fixed-output-test-report.txt").write_text(report)
    (output / "source-sha256.txt").write_text("".join(
        hashlib.sha256((native / name).read_bytes()).hexdigest() + "  " + name + "\n"
        for name in ["MusicEngine.cpp", "MusicEngine.h", "OutputStage.h"]))
    print(report, end="")
    if result.returncode:
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
