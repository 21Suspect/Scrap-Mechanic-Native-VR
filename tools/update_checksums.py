#!/usr/bin/env python3
"""Regenerate the public checksum list from the guarded manifest."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
    paths = ["payload/" + entry["path"].replace("\\", "/") for entry in manifest["files"]]
    paths.extend(
        (
            "assets/ScrapMechanicVR-Logo.png",
            "assets/audio/BOOMBOX MIX NEW.mp3",
            "dist/ScrapMechanicVR-Installer.exe",
        )
    )
    lines = []
    for relative in paths:
        path = root / relative
        if not path.is_file():
            raise FileNotFoundError(path)
        lines.append(f"{sha256(path)}  {relative}")
    (root / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote SHA256SUMS.txt with {len(lines)} entries")


if __name__ == "__main__":
    main()
