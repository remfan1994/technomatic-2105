#!/usr/bin/env python3
"""Host AudioEngine lifecycle regression test with Oboe/log stubs and real MusicEngine."""
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
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="technomatic-controls-"))
    output.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("C++17 compiler required")
    source = root / "app/src/main/cpp"
    executable = output / "audio-controls-v29"
    command = [compiler, "-std=c++17", "-O2", "-pthread", "-Wall", "-Wextra",
               "-I", str(root / "tools/audio_control_stubs"), "-I", str(source),
               str(root / "tools/audio_controls_v29.cpp"), str(source / "AudioEngine.cpp"),
               str(source / "MusicEngine.cpp"), "-o", str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True, timeout=30)
    report = result.stdout + "\n".join([
        "Host test compiles production AudioEngine and MusicEngine with test-only Oboe/log stubs.",
        "It verifies control ordering and identity; it does not emulate a real Android audio backend.",
        "Oboe guarantees the stream is stopped and closed before onErrorAfterClose:",
        "https://google.github.io/oboe/classoboe_1_1_audio_stream_error_callback.html",
    ]) + "\n"
    (output / "audio-control-test-report.txt").write_text(report, encoding="ascii")
    print(report, end="")
    print("Report: " + str(output / "audio-control-test-report.txt"))


if __name__ == "__main__":
    main()
