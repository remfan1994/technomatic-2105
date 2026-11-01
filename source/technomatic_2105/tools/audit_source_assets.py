#!/usr/bin/env python3
"""Inventory shipped music assets and explicit sample-loading paths.

This is a source inventory, not a musical-similarity or copyright detector.
Run from any directory; optional APKs add a packaged-resource inventory.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path


AUDIO_SUFFIXES = {
    ".wav", ".wave", ".mp3", ".ogg", ".opus", ".flac", ".aac", ".m4a",
    ".aif", ".aiff", ".mid", ".midi", ".sf2", ".sfz", ".dls",
    ".mod", ".xm", ".s3m", ".it", ".rmi", ".pcm", ".raw",
}
SOURCE_SUFFIXES = {".java", ".cpp", ".h", ".xml", ".gradle", ".txt"}
REFERENCE = re.compile(r"Nintendo|Zelda|MediaPlayer|SoundPool|SoundFont|AAssetManager|AAsset_open", re.I)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--apk", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.project.resolve()
    app = root / "app/src/main"
    files = sorted(p for p in app.rglob("*") if p.is_file())
    rel = lambda p: p.relative_to(root).as_posix()
    references = []
    for path in files:
        if path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if REFERENCE.search(line):
                references.append({"path": rel(path), "line": number, "text": line.strip()})
    manifest = (app / "AndroidManifest.xml").read_text(encoding="utf-8")
    result = {
        "version": "v30",
        "scope": "Production app/src/main files and explicitly supplied APKs; no similarity detection.",
        "production_file_count": len(files),
        "audio_or_sequence_assets": [rel(p) for p in files if p.suffix.lower() in AUDIO_SUFFIXES],
        "assets_or_raw_files": [rel(p) for p in files if "/assets/" in rel(p) or "/res/raw/" in rel(p)],
        "internet_permission": "android.permission.INTERNET" in manifest,
        "named_cue_or_sample_loader_references": references,
        "production_sha256": {rel(p): sha256(p) for p in files},
        "apks": [],
    }
    for apk in args.apk:
        with zipfile.ZipFile(apk) as archive:
            entries = archive.namelist()
            result["apks"].append({
                "filename": apk.name,
                "sha256": sha256(apk),
                "zip_crc_failure": archive.testzip(),
                "audio_or_sequence_assets": [n for n in entries if Path(n).suffix.lower() in AUDIO_SUFFIXES],
                "assets_or_raw_files": [n for n in entries if n.startswith("assets/") or n.startswith("res/raw/")],
                "native_libraries": sorted(n for n in entries if n.startswith("lib/") and n.endswith(".so")),
            })
    encoded = json.dumps(result, indent=2, ensure_ascii=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
        print(str(args.output))
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
