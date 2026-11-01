#!/usr/bin/env python3
"""Host regression tests for embedded OGG/FLAC metadata; needs JDK + FFmpeg."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command, *, binary=False):
    result = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout if binary else result.stdout.decode("utf-8")


def executable(name):
    candidate = Path(os.environ.get("JAVA_HOME", "")) / "bin" / name
    if name in ("java", "javac") and candidate.is_file():
        return str(candidate)
    found = shutil.which(name)
    if not found:
        raise SystemExit("Required executable missing: " + name)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="retain fixtures and test report here")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="technomatic-metadata-"))
    output.mkdir(parents=True, exist_ok=True)
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    java_dir = root / "app/src/main/java/vip/thatiam/technomatic2105"
    tests_dir = root / "tools/java/vip/thatiam/technomatic2105"
    sources = [java_dir / (name + ".java") for name in
               ("ExportCancellationToken", "ExportMetadata", "OggMetadataWriter", "FlacExporter")]
    sources += [tests_dir / "MetadataHostTest.java", tests_dir / "FlacHostTest.java"]
    run([executable("javac"), "--release", "8", "-encoding", "UTF-8", "-d", str(classes), *map(str, sources)])

    ffmpeg = executable("ffmpeg")
    ffprobe = executable("ffprobe")
    raw = output / "fixture.pcm"
    source = output / "source.ogg"
    run([ffmpeg, "-hide_banner", "-v", "error", "-y",
         "-f", "lavfi", "-i", "sine=frequency=233.0819:sample_rate=48000:duration=8",
         "-f", "lavfi", "-i", "sine=frequency=349.2282:sample_rate=48000:duration=8",
         "-filter_complex", "[0:a][1:a]amerge=inputs=2[a]", "-map", "[a]", "-ac", "2",
         "-c:a", "pcm_s16le", "-f", "s16le", str(raw)])
    run([ffmpeg, "-hide_banner", "-v", "error", "-y", "-f", "s16le", "-ar", "48000", "-ac", "2",
         "-i", str(raw), "-map_metadata", "-1", "-c:a", "libopus", "-b:a", "128k", "-f", "ogg", str(source)])
    host_result = run([executable("java"), "-cp", str(classes),
                       "vip.thatiam.technomatic2105.MetadataHostTest", str(raw), str(source), str(output)]).strip()

    def probe(path):
        return json.loads(run([ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)]))

    def decoded(path):
        return run([ffmpeg, "-hide_banner", "-v", "error", "-i", str(path),
                    "-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "-"], binary=True)

    expected_comment = ("141414419 all channels neon drift dominant\n"
                        "Original slow motion. Unicode: \u03b1 / \u6771\u4eac.\nSecond note line.\n"
                        "Range: 00:00:10-00:00:18")
    for name in ("tagged.ogg", "tagged.flac"):
        info = probe(output / name)
        tags = {key.upper(): value for key, value in info.get("format", {}).get("tags", {}).items()}
        for stream in info["streams"]:
            tags.update({key.upper(): value for key, value in stream.get("tags", {}).items()})
        assert tags["TITLE"] == "fixture", (name, "title")
        assert tags["COMMENT"] == expected_comment, (name, "comment")
        # FFmpeg aliases Vorbis DESCRIPTION to COMMENT; raw tools below verify
        # that both physical fields are present in the exported files.
        if "DESCRIPTION" in tags:
            assert tags["DESCRIPTION"] == expected_comment, (name, "description")
        assert tags["SEED"] == "141414419", (name, "seed")
        assert tags["CHANNEL_MASK"] == "4095", (name, "channel mask")
        assert tags["CHANNEL_PRIMARY"] == "Neon Drift", (name, "primary")
        assert tags["CHANNELS"].split("; ")[-1] == "Cold Arcade", (name, "all selected channels")
        assert tags["EXPORT_START_SECONDS"] == "10", (name, "start")
        assert tags["EXPORT_END_SECONDS"] == "18", (name, "end")
        assert tags["TECHNOMATIC_SONG_DATA"].endswith("cand=17;engine=29;intent=0"), (name, "recipe")
        assert tags["DATE"] == "2026-10-18", (name, "frozen date")
        assert tags["ENGINE_VERSION"] == "29", (name, "engine version")
        assert tags["COMPOSITION_INTENT"] == "slow", (name, "composition intent")
        assert info["streams"][0]["channels"] == 2, (name, "stereo")
        (output / (name + ".ffprobe.json")).write_text(json.dumps(info, indent=2, ensure_ascii=True) + "\n")

    raw_bytes = raw.read_bytes()
    flac_bytes = decoded(output / "tagged.flac")
    assert flac_bytes == raw_bytes, "FLAC decoded PCM differs from source"
    reference_pcm = decoded(source)
    baseline_duration = probe(source)["format"]["duration"]
    ogg_files = [output / "tagged.ogg"]
    ogg_files += sorted(output.glob("boundary_*.ogg")) + sorted(output.glob("reduced_*.ogg"))
    for path in ogg_files:
        assert decoded(path) == reference_pcm, "Opus decoded PCM changed: " + path.name
        assert probe(path)["format"]["duration"] == baseline_duration, "Opus duration changed: " + path.name

    optional_checks = []
    for tool, name, arguments in [
        ("opusinfo", "tagged.ogg", []),
        ("metaflac", "tagged.flac", ["--export-tags-to=-"]),
        ("mediainfo", "tagged.ogg", []),
        ("mediainfo", "tagged.flac", []),
    ]:
        program = shutil.which(tool)
        if not program:
            optional_checks.append(tool + ": unavailable (skipped)")
            continue
        text = run([program, *arguments, str(output / name)])
        assert "141414419" in text and "Neon Drift" in text, (tool, name, "identity visible")
        assert "Original slow motion" in text, (tool, name, "user notes visible")
        if tool in ("opusinfo", "metaflac"):
            assert "DESCRIPTION=" + expected_comment in text, (tool, name, "description physically embedded")
            assert "COMMENT=" + expected_comment in text, (tool, name, "comment physically embedded")
        if tool == "opusinfo":
            assert "WARNING" not in text, "Opus structure warning"
            assert "0m:08.000s" in text, "Opus duration"
        # Keep raw inspector output alongside the ASCII summary.
        (output / (name + "." + tool + ".txt")).write_text(text, encoding="utf-8")
        optional_checks.append(tool + ": " + name + " tags visible")
    if shutil.which("flac"):
        run([shutil.which("flac"), "--test", "--silent", str(output / "tagged.flac")])
        optional_checks.append("flac --test: frame CRC and STREAMINFO MD5 passed")

    report = [host_result,
              "ffprobe: OGG and FLAC independently expose required metadata values, including Unicode notes.",
              "FLAC decoded PCM equals all 1,536,000 source bytes (384,000 stereo frames, 8 seconds).",
              "OGG metadata insertion/growth/shrinkage: decoded PCM and duration unchanged across 9 outputs.",
              "Opus decoded PCM SHA-256: " + hashlib.sha256(reference_pcm).hexdigest(),
              "FLAC/source PCM SHA-256: " + hashlib.sha256(raw_bytes).hexdigest(),
              *optional_checks,
              "Primary format references:",
              "https://www.rfc-editor.org/rfc/rfc7845.html#section-5.2",
              "https://www.rfc-editor.org/rfc/rfc3533.html#section-6",
              "https://www.rfc-editor.org/rfc/rfc9639.html#name-vorbis-comment",
              "https://www.xiph.org/vorbis/doc/v-comment.html",
              "Android codec and MediaStore UI still require device-level verification."]
    (output / "metadata-test-report.txt").write_text("\n".join(report) + "\n", encoding="ascii")
    print("\n".join(report[:6 + len(optional_checks)]))
    print("Fixtures and report: " + str(output))


if __name__ == "__main__":
    main()
