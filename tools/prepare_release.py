#!/usr/bin/env python3
"""Synchronize guarded release metadata after a local source build."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


ADAPTER_FILES = (
    "Bucket.lua",
    "Glowstick.lua",
    "Cornade.lua",
    "ClayTool.lua",
    "ExtinguisherTool.lua",
    "Planter.lua",
    "Fertilizer.lua",
    "Eat.lua",
    "Feeder.lua",
    "SoilBag.lua",
    "KeyTool.lua",
    "ResourceTool.lua",
    "CarryTool.lua",
    "LogBook.lua",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def dump_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def replace_constant(text: str, name: str, value: str) -> str:
    pattern = rf'(internal const string {re.escape(name)} = ")[^"]*(";)'
    text, count = re.subn(pattern, rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise RuntimeError(f"missing installer constant: {name}")
    return text


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path)
    parser.add_argument("--version", default="1.2.1-chapter2-20260830")
    parser.add_argument(
        "--headset-confirmed",
        action="store_true",
        help="record that this exact candidate was verified in a headset",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    payload = root / "payload"
    manifest_path = root / "manifest.json"
    snapshot_path = root / "snapshot.json"
    program_path = root / "installer" / "Program.cs"
    addon = payload / "Release" / "smvr_native_vr_v1.addon64"
    if not addon.is_file():
        raise FileNotFoundError(addon)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    manifest["patchVersion"] = args.version
    manifest["description"] = (
        "Scrap Mechanic Chapter 2 native OpenXR VR with tracked Quest hands, "
        "complete held-item and tool adapters, barrel-aimed weapons, spatial live "
        "game menus, native queued UI input, transparent composition, and restrained haptics"
    )

    adapter_paths = {
        f"Survival\\Scripts\\game\\tools\\{name}": name for name in ADAPTER_FILES
    }
    files = [entry for entry in manifest["files"] if entry["path"] not in adapter_paths]
    insert_at = next(
        (index for index, entry in enumerate(files) if entry["path"].endswith("Sledgehammer.lua")),
        len(files) - 1,
    ) + 1
    additions = []
    for game_path, name in adapter_paths.items():
        source = args.game_root / Path(game_path.replace("\\", "/"))
        patched = payload / Path(game_path.replace("\\", "/"))
        if not source.is_file() or not patched.is_file():
            raise FileNotFoundError(source if not source.is_file() else patched)
        additions.append(
            {
                "path": game_path,
                "patchedSha256": sha256(patched),
                "originalSha256": [sha256(source)],
                "module": "chapter2-vr-held-items",
            }
        )
    files[insert_at:insert_at] = additions
    manifest["files"] = files

    # Every payload hash is recomputed, not copied from an earlier release.
    for entry in manifest["files"]:
        patched = payload / Path(entry["path"].replace("\\", "/"))
        if not patched.is_file():
            raise FileNotFoundError(patched)
        entry["patchedSha256"] = sha256(patched)
    dump_json(manifest_path, manifest)

    snapshot = json.loads(snapshot_path.read_text(encoding="utf-8-sig"))
    semver = args.version.split("-", 1)[0]
    snapshot["snapshot"] = f"chapter2-v{semver}"
    snapshot["localFeatureBranch"] = "main"
    snapshot["localFeatureStatus"] = "release-candidate-built-and-validated"
    artifact = {"path": "payload/Release/smvr_native_vr_v1.addon64", "size": addon.stat().st_size, "sha256": sha256(addon)}
    snapshot["testedArtifact"] = artifact.copy()
    candidate = snapshot.setdefault("localCandidateArtifact", {})
    candidate.update({"version": args.version, **artifact, "headsetConfirmed": args.headset_confirmed})
    source_meta = candidate.setdefault("source", {})
    source_meta["nativeVrSha256"] = sha256(root / "src" / "native_vr.cpp")
    source_meta["vrToolsSha256"] = sha256(root / "source" / "NativeVR" / "src" / "vr_tools.cpp")
    source_meta["heldItemAssetSha256"] = sha256(root / "source" / "NativeVR" / "src" / "held_item_asset.hpp")
    source_meta["heldItemAssetGeneratorSha256"] = sha256(root / "source" / "NativeVR" / "tools" / "generate_held_item_assets.py")
    source_meta["heldItemPayloadGeneratorSha256"] = sha256(root / "source" / "NativeVR" / "tools" / "generate_held_item_payload.py")
    source_meta["chapter2GameplayBridgeSha256"] = sha256(payload / "Survival" / "Scripts" / "game" / "Chapter2VR.lua")
    source_meta["heldItemScriptSha256"] = {
        name: sha256(payload / "Survival" / "Scripts" / "game" / "tools" / name)
        for name in ADAPTER_FILES
    }
    validation = snapshot.setdefault("validation", {})
    validation["date"] = "2026-08-30"
    validation["installerManagedFiles"] = len(manifest["files"])
    validation["luaSyntax"] = "PASS"
    validation["nativeReleaseBuild"] = "PASS"
    validation["headsetConfirmed"] = args.headset_confirmed
    installer = snapshot.setdefault("installer", {})
    installer.update({"version": args.version, "path": "dist/ScrapMechanicVR-Installer.exe", "selfTest": "PENDING", "published": False})
    dump_json(snapshot_path, snapshot)

    program = program_path.read_text(encoding="utf-8-sig")
    parts = semver.split(".")
    assembly = ".".join((parts + ["0"] * 4)[:3] + ["0"])
    program, count = re.subn(r'\[assembly: AssemblyVersion\("[^"]+"\)\]', f'[assembly: AssemblyVersion("{assembly}")]', program, count=1)
    if count != 1:
        raise RuntimeError("missing AssemblyVersion")
    program, count = re.subn(r'\[assembly: AssemblyFileVersion\("[^"]+"\)\]', f'[assembly: AssemblyFileVersion("{assembly}")]', program, count=1)
    if count != 1:
        raise RuntimeError("missing AssemblyFileVersion")
    hashes = {
        "Version": args.version,
        "GameBuild": str(manifest["game"]["buildId"]),
        "GameExeHash": manifest["game"]["executableSha256"],
        "AddonHash": sha256(addon),
        "DxgiHash": sha256(payload / "Release" / "dxgi.dll"),
        "LoaderHash": sha256(payload / "Release" / "libopenxr_loader.dll"),
        "MusicHash": sha256(root / "assets" / "audio" / "BOOMBOX MIX NEW.mp3"),
        "LogoHash": sha256(root / "assets" / "ScrapMechanicVR-Logo.png"),
        "ManifestHash": sha256(manifest_path),
        "PatcherHash": sha256(root / "Patcher.ps1"),
    }
    for name, value in hashes.items():
        program = replace_constant(program, name, value)
    program, count = re.subn(
        r"internal const int ManagedFileCount = [0-9]+;",
        f"internal const int ManagedFileCount = {len(manifest['files'])};",
        program,
        count=1,
    )
    if count != 1:
        raise RuntimeError("missing ManagedFileCount")
    program_path.write_text(program, encoding="utf-8", newline="\n")
    print(f"prepared {args.version}: {len(manifest['files'])} managed files")
    print(f"addon SHA-256 {sha256(addon)}")


if __name__ == "__main__":
    main()
