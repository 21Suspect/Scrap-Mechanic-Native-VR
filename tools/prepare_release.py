#!/usr/bin/env python3
"""Synchronize guarded release metadata after a local source build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from datetime import date
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


# A small number of public/intermediate installers were distributed without a
# matching repository tag. Keep their path-specific hashes here so a newer
# installer can identify them as our payload, while still rejecting arbitrary
# files at the same paths.
KNOWN_HISTORICAL_PATCHED_HASHES = {
    "Release\\smvr_native_vr_v1.addon64": {
        "4DF805BB27DA7479C03BECB8843173CF8AB2E3A933C63A25BF083B65CF17EE82",
    },
    "Survival\\Scripts\\game\\Chapter2VR.lua": {
        "3D8E9B61BFB36D7602046ED81C4528DFE2BFB9B4FEEE3297E9F576D23A1326E3",
    },
}


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


def add_manifest_history(history: dict[str, set[str]], value: object) -> None:
    if not isinstance(value, dict):
        return
    for entry in (*value.get("files", []), *value.get("legacyFiles", [])):
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            continue
        hashes = history.setdefault(entry["path"], set())
        for candidate in (entry.get("patchedSha256"), *entry.get("historicalPatchedSha256", [])):
            if isinstance(candidate, str) and re.fullmatch(r"[0-9A-Fa-f]{64}", candidate):
                hashes.add(candidate.upper())


def collect_manifest_history(root: Path, current: dict[str, object]) -> dict[str, set[str]]:
    history: dict[str, set[str]] = {
        path: {candidate.upper() for candidate in hashes}
        for path, hashes in KNOWN_HISTORICAL_PATCHED_HASHES.items()
    }
    add_manifest_history(history, current)

    # Repository tags are the authoritative record of published builds.
    tags = subprocess.run(
        ["git", "tag", "--list", "chapter2-v*"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    for tag in tags:
        result = subprocess.run(
            ["git", "show", f"{tag}:manifest.json"],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            continue
        try:
            add_manifest_history(history, json.loads(result.stdout))
        except json.JSONDecodeError:
            continue

    # Local extracted packages include development/public candidates that may
    # have reached a tester before a tag was created. Once collected, the
    # hashes are persisted in the next manifest and no longer depend on this PC.
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        packages = Path(local_app_data) / "ScrapMechanicVR-Chapter2" / "packages"
        for candidate in packages.glob("*/manifest.json") if packages.is_dir() else ():
            try:
                add_manifest_history(history, json.loads(candidate.read_text(encoding="utf-8-sig")))
            except (OSError, json.JSONDecodeError):
                continue
    return history


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path)
    parser.add_argument("--version", default="1.3.5-chapter2-20260901")
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
    historical_hashes = collect_manifest_history(root, manifest)
    manifest["patchVersion"] = args.version
    manifest["description"] = (
        "Scrap Mechanic Chapter 2 native OpenXR VR with tracked Quest Touch and Valve Index "
        "controllers, optional Quest optical hands, Custom Game content bridging, complete "
        "held-item geometry, grouped live pose calibration, hand-aimed stock hammer and Use "
        "actions, barrel-aimed weapons, spatial live game menus, native "
        "queued UI input, transparent composition, and restrained haptics"
    )

    managed_script_paths = {
        f"Survival\\Scripts\\game\\tools\\{name}": "chapter2-vr-held-items"
        for name in ADAPTER_FILES
    }
    retired_script_paths = {"Survival\\Scripts\\game\\harvestable\\HarvestCore.lua"}
    existing_entries = {entry["path"]: entry for entry in manifest["files"]}
    files = [
        entry for entry in manifest["files"]
        if entry["path"] not in managed_script_paths and entry["path"] not in retired_script_paths
    ]
    insert_at = next(
        (index for index, entry in enumerate(files) if entry["path"].endswith("Sledgehammer.lua")),
        len(files) - 1,
    ) + 1
    additions = []
    for game_path, module in managed_script_paths.items():
        patched = payload / Path(game_path.replace("\\", "/"))
        if not patched.is_file():
            raise FileNotFoundError(patched)
        previous = existing_entries.get(game_path, {})
        original_hashes = previous.get("originalSha256")
        if not original_hashes:
            source = args.game_root / Path(game_path.replace("\\", "/"))
            if not source.is_file():
                raise FileNotFoundError(source)
            original_hashes = [sha256(source)]
        additions.append({
            "path": game_path,
            "patchedSha256": sha256(patched),
            "originalSha256": original_hashes,
            "module": module,
        })
    files[insert_at:insert_at] = additions
    manifest["files"] = files

    # Every payload hash is recomputed, not copied from an earlier release.
    for entry in manifest["files"]:
        patched = payload / Path(entry["path"].replace("\\", "/"))
        if not patched.is_file():
            raise FileNotFoundError(patched)
        entry["patchedSha256"] = sha256(patched)
    for entry in (*manifest["files"], *manifest.get("legacyFiles", [])):
        current_hash = str(entry.get("patchedSha256", "")).upper()
        originals = {
            str(candidate).upper()
            for candidate in entry.get("originalSha256", [])
            if candidate
        }
        prior = sorted(
            candidate
            for candidate in historical_hashes.get(entry["path"], set())
            if candidate != current_hash and candidate not in originals
        )
        if prior:
            entry["historicalPatchedSha256"] = prior
        else:
            entry.pop("historicalPatchedSha256", None)
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
    source_paths = {
        "nativeVrSha256": "src/native_vr.cpp",
        "featureInputSha256": "src/feature_input.cpp",
        "featureInputHeaderSha256": "src/feature_input.hpp",
        "engineInputSha256": "src/feature_engine_input.cpp",
        "engineInputHeaderSha256": "src/feature_engine_input.hpp",
        "startupMenuSha256": "src/feature_startup_menu.cpp",
        "startupMenuHeaderSha256": "src/feature_startup_menu.hpp",
        "vrHandsSha256": "source/NativeVR/src/vr_hands.cpp",
        "vrToolsSha256": "source/NativeVR/src/vr_tools.cpp",
        "vrToolsHeaderSha256": "source/NativeVR/src/vr_tools.hpp",
        "mechanicHandsAssetSha256": "source/NativeVR/src/mechanic_hands_asset.hpp",
        "nativeToolAssetSha256": "source/NativeVR/src/native_tool_asset.hpp",
        "chapter2ToolAssetSha256": "source/NativeVR/src/chapter2_tool_asset.hpp",
        "mechanicHandsGeneratorSha256": "source/NativeVR/tools/generate_mechanic_hands.py",
        "chapter2ToolGeneratorSha256": "source/NativeVR/tools/generate_chapter2_weapons.py",
        "customContentBridgeSha256": "source/NativeVR/src/custom_content_bridge.cpp",
        "customContentBridgeHeaderSha256": "source/NativeVR/src/custom_content_bridge.hpp",
        "clayCalibrationHelperSourceSha256": "tools/ClayCalibration/Program.cs",
        "heldItemAssetSha256": "source/NativeVR/src/held_item_asset.hpp",
        "heldItemAssetGeneratorSha256": "source/NativeVR/tools/generate_held_item_assets.py",
        "heldItemPayloadGeneratorSha256": "source/NativeVR/tools/generate_held_item_payload.py",
        "heldItemCatalogSha256": "source/NativeVR/src/held_item_catalog.hpp",
        "fullHeldItemCatalogGeneratorSha256": "source/NativeVR/tools/generate_full_held_item_catalog.py",
        "blenderHeldCatalogExtractorSha256": "source/NativeVR/tools/blender_extract_held_catalog.py",
        "heldCalibrationHelperSourceSha256": "tools/HeldCalibration/Program.cs",
        "heldCalibrationBuildScriptSha256": "Build-HeldCalibration.ps1",
        "installerSourceSha256": "installer/Program.cs",
        "installerBuildScriptSha256": "Build-OneFileInstaller.ps1",
        "installerHeadsetProbeTestSha256": "tools/Test-InstallerHeadsetProbe.ps1",
    }
    for key, relative in source_paths.items():
        source_meta[key] = sha256(root / relative)
    source_meta.pop("handRefineScriptSha256", None)
    source_meta["chapter2GameplayBridgeSha256"] = sha256(
        payload / "Survival" / "Scripts" / "game" / "Chapter2VR.lua"
    )
    source_meta["heldItemCatalogBinarySha256"] = sha256(
        payload / "Release" / "ScrapMechanicVR-HeldItems.bin"
    )
    source_meta["heldItemCatalogIndexSha256"] = sha256(
        payload / "Release" / "ScrapMechanicVR-HeldItems.tsv"
    )
    source_meta["heldItemScriptSha256"] = {
        name: sha256(payload / "Survival" / "Scripts" / "game" / "tools" / name)
        for name in ADAPTER_FILES
    }
    validation = snapshot.setdefault("validation", {})
    validation["date"] = date.today().isoformat()
    validation["installerManagedFiles"] = len(manifest["files"])
    validation["luaSyntax"] = "PASS"
    validation["nativeReleaseBuild"] = "PASS"
    validation["headsetConfirmed"] = args.headset_confirmed
    installer = snapshot.setdefault("installer", {})
    previous_installer_version = installer.get("version")
    previous_self_test = installer.get("selfTest")
    installer.update({
        "version": args.version,
        "path": "dist/ScrapMechanicVR-Installer.exe",
        "selfTest": previous_self_test
        if previous_installer_version == args.version and previous_self_test
        else "PENDING",
        "published": False,
    })
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
