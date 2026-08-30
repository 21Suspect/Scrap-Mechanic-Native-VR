#!/usr/bin/env python3
"""Generate complete, lazy-loaded VR coverage for Chapter 2 shapes.

The two shipping shape-set databases are treated as the source of truth.  All
block and part UUIDs are catalogued.  FBX geometry is converted by Blender,
DAE geometry uses the existing deterministic Collada reader, and unsupported
proprietary meshes receive an identifiable bounded proxy instead of vanishing.

Outputs:
  * held_item_catalog.hpp: compact UUID/profile/mesh metadata for the add-on
  * ScrapMechanicVR-HeldItems.bin: interleaved vertices, loaded on item switch
  * ScrapMechanicVR-HeldItems.tsv: names/profiles consumed by the live helper
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from generate_chapter2_weapons import extract


CATALOG_PROFILES = (
    "blocks",
    "wedges",
    "small_parts",
    "medium_parts",
    "large_parts",
    "consumables",
    "resources",
    "components",
    "plantables",
    "quest_items",
    "other_parts",
)

PROFILE_LABELS = {
    "blocks": "Blocks",
    "wedges": "Wedges",
    "small_parts": "Small Parts",
    "medium_parts": "Medium Parts",
    "large_parts": "Large Parts",
    "consumables": "Consumables",
    "resources": "Resources",
    "components": "Components",
    "plantables": "Plantables",
    "quest_items": "Quest / Special Items",
    "other_parts": "Other Parts",
}

PACKED_VERTEX_FORMAT = "<3f3h2e"
PACKED_VERTEX_SIZE = struct.calcsize(PACKED_VERTEX_FORMAT)


def packed_vertex(vertex: tuple[float, ...]) -> bytes:
    normal = [max(-1.0, min(1.0, float(vertex[index]))) for index in range(3, 6)]
    uv = [max(-65504.0, min(65504.0, float(vertex[index]))) for index in range(6, 8)]
    return struct.pack(
        PACKED_VERTEX_FORMAT,
        float(vertex[0]), float(vertex[1]), float(vertex[2]),
        *(int(round(value * 32767.0)) for value in normal),
        *uv,
    )


def resolve_game_path(game_root: Path, value: str) -> Path:
    relative = (value.replace("$GAME_DATA/", "Data/")
                     .replace("$SURVIVAL_DATA/", "Survival/")
                     .replace("$CHALLENGE_DATA/", "ChallengeData/"))
    return game_root / relative


def game_relative(value: str | None) -> str | None:
    if not value:
        return None
    return (value.replace("$GAME_DATA/", "Data/")
                 .replace("$SURVIVAL_DATA/", "Survival/")
                 .replace("$CHALLENGE_DATA/", "ChallengeData/")
                 .replace("/", "\\"))


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def color_rgba(value: str | None) -> int:
    text = (value or "ffffff").lstrip("#")
    if len(text) == 6:
        text += "ff"
    if len(text) != 8 or not re.fullmatch(r"[0-9a-fA-F]{8}", text):
        text = "ffffffff"
    red, green, blue, alpha = (int(text[index:index + 2], 16) for index in range(0, 8, 2))
    return red | green << 8 | blue << 16 | alpha << 24


def diffuse(value: dict) -> str | None:
    textures = value.get("textures") or {}
    result = textures.get("diffuse")
    if not result and value.get("textureList"):
        result = value["textureList"][0]
    return result or None


def render_description(game_root: Path, item: dict) -> dict | None:
    value = item.get("renderable")
    if isinstance(value, dict):
        return value
    if isinstance(value, str):
        path = resolve_game_path(game_root, value)
        if path.is_file():
            return read_json(path)
    return None


def lod_materials(lod: dict) -> tuple[dict[str, str | None], list[str | None] | None]:
    if isinstance(lod.get("subMeshMap"), dict):
        return {name: diffuse(value) for name, value in lod["subMeshMap"].items()}, None
    return {}, [diffuse(value) for value in lod.get("subMeshList", [])]


def normalized_material(value: str) -> str:
    value = value.rsplit("::", 1)[-1]
    return re.sub(r"\.\d{3}$", "", value).lower()


def texture_for_group(group: dict, by_name: dict[str, str | None], ordered: list[str | None] | None) -> str | None:
    material = group["material"]
    if material in by_name:
        return by_name[material]
    normalized = normalized_material(material)
    for name, value in by_name.items():
        if normalized_material(name) == normalized:
            return value
    order = int(group.get("order", 0))
    if ordered is not None and 0 <= order < len(ordered):
        return ordered[order]
    return None


def box_vertices() -> list[tuple[float, ...]]:
    faces = (
        ((1, 0, 0), ((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1))),
        ((-1, 0, 0), ((-1, 1, -1), (-1, -1, -1), (-1, -1, 1), (-1, 1, 1))),
        ((0, 1, 0), ((-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1))),
        ((0, -1, 0), ((1, -1, -1), (-1, -1, -1), (-1, -1, 1), (1, -1, 1))),
        ((0, 0, 1), ((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1))),
        ((0, 0, -1), ((-1, 1, -1), (1, 1, -1), (1, -1, -1), (-1, -1, -1))),
    )
    uvs = ((0, 1), (1, 1), (1, 0), (0, 0))
    vertices = []
    for normal, corners in faces:
        for index in (0, 1, 2, 0, 2, 3):
            point, uv = corners[index], uvs[index]
            vertices.append((*point, *normal, *uv))
    return vertices


def append_vertices(stream, vertices: list[tuple[float, ...]]) -> tuple[int, int]:
    first = stream.tell() // PACKED_VERTEX_SIZE
    for vertex in vertices:
        stream.write(packed_vertex(vertex))
    return first, len(vertices)


def append_dae(path: Path, stream) -> dict:
    _, groups = extract(path)
    all_vertices = [vertex for _, _, _, values in groups for vertex in values]
    if not all_vertices:
        raise RuntimeError("DAE contains no triangles")
    minimum = [min(value[axis] for value in all_vertices) for axis in range(3)]
    maximum = [max(value[axis] for value in all_vertices) for axis in range(3)]
    center = [(minimum[axis] + maximum[axis]) * 0.5 for axis in range(3)]
    longest = max(maximum[axis] - minimum[axis] for axis in range(3))
    if not math.isfinite(longest) or longest < 1.0e-7:
        raise RuntimeError("DAE has invalid bounds")
    scale = 2.0 / longest
    emitted = []
    for order, (_, material, _, vertices) in enumerate(groups):
        normalized = [
            ((vertex[0] - center[0]) * scale, (vertex[1] - center[1]) * scale,
             (vertex[2] - center[2]) * scale, *vertex[3:])
            for vertex in vertices
        ]
        first, count = append_vertices(stream, normalized)
        emitted.append({"material": material, "order": order, "first": first, "count": count})
    return {"groups": emitted, "bounds": {"sourceMin": minimum, "sourceMax": maximum, "normalization": scale}}


def shape_dimension(item: dict) -> float:
    for key in ("box", "hull"):
        value = item.get(key)
        if isinstance(value, dict):
            dimensions = [float(value.get(axis, 0.0)) for axis in ("x", "y", "z")]
            if max(dimensions, default=0.0) > 0:
                return max(dimensions)
    cylinder = item.get("cylinder")
    if isinstance(cylinder, dict):
        return max(float(cylinder.get("depth", 0.0)), float(cylinder.get("diameter", 0.0)))
    sphere = item.get("sphere")
    if isinstance(sphere, dict):
        return float(sphere.get("diameter", sphere.get("radius", 1.0) * 2.0))
    return 3.0


def profile_for(kind: str, item: dict, source: Path) -> str:
    stem = source.stem.lower()
    name = str(item.get("name") or "").lower()
    if kind == "blockList":
        return "blocks"
    if "wedge" in stem or "wedge" in name or "ramp" in name:
        return "wedges"
    if "consumable" in stem or item.get("edible"):
        return "consumables"
    if "resource" in stem or "harvest" in stem or "voxelmaterial" in stem:
        return "resources"
    if "component" in stem or stem == "tool_parts":
        return "components"
    if "plantable" in stem:
        return "plantables"
    if any(token in stem for token in ("quest", "artifact", "jewel", "reward", "outfit", "characterobject")):
        return "quest_items"
    dimension = shape_dimension(item)
    if dimension <= 2.25:
        return "small_parts"
    if dimension <= 5.0:
        return "medium_parts"
    if dimension > 5.0:
        return "large_parts"
    return "other_parts"


def cpp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ") + '"'


def cpp_wstring(value: str | None) -> str:
    return "nullptr" if not value else "L" + cpp_string(value)


def collect_items(game_root: Path) -> dict[str, tuple[str, dict, Path]]:
    shape_files: list[Path] = []
    for master in (game_root / "Data/Objects/Database/shapesets.json",
                   game_root / "Survival/Objects/Database/shapesets.json"):
        for value in read_json(master).get("shapeSetList", []):
            path = resolve_game_path(game_root, value)
            if path.is_file() and path not in shape_files:
                shape_files.append(path)
    result: dict[str, tuple[str, dict, Path]] = {}
    for path in shape_files:
        data = read_json(path)
        for kind in ("blockList", "partList"):
            for item in data.get(kind, []):
                uuid = item.get("uuid")
                if uuid and uuid not in result:
                    result[uuid] = kind, item, path
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_root", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("vertices", type=Path)
    parser.add_argument("catalog_tsv", type=Path)
    parser.add_argument("--blender", type=Path,
                        default=Path(r"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe"))
    args = parser.parse_args()
    if not args.blender.is_file():
        raise FileNotFoundError(args.blender)

    items = collect_items(args.game_root)
    render_info: dict[str, tuple[dict, dict, str]] = {}
    fbx_tasks: dict[str, Path] = {}
    for uuid, (kind, item, _) in items.items():
        if kind == "blockList":
            continue
        rend = render_description(args.game_root, item)
        if not rend or not rend.get("lodList"):
            continue
        lod = rend["lodList"][0]
        mesh = lod.get("mesh")
        if not isinstance(mesh, str):
            continue
        render_info[uuid] = rend, lod, mesh
        path = resolve_game_path(args.game_root, mesh)
        if path.suffix.lower() == ".fbx" and path.is_file():
            fbx_tasks[mesh] = path

    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.vertices.parent.mkdir(parents=True, exist_ok=True)
    args.catalog_tsv.parent.mkdir(parents=True, exist_ok=True)
    blender_script = Path(__file__).with_name("blender_extract_held_catalog.py")
    with tempfile.TemporaryDirectory(prefix="smvr-held-catalog-") as temporary:
        temporary = Path(temporary)
        tasks_path = temporary / "tasks.json"
        meta_path = temporary / "fbx-meta.json"
        tasks = [{"key": key, "path": str(path)} for key, path in sorted(fbx_tasks.items())]
        tasks_path.write_text(json.dumps(tasks), encoding="utf-8")
        command = [str(args.blender), "--background", "--factory-startup", "--python", str(blender_script),
                   "--", str(tasks_path), str(args.vertices), str(meta_path)]
        subprocess.run(command, check=True)
        mesh_meta = json.loads(meta_path.read_text(encoding="utf-8"))

    with args.vertices.open("ab") as stream:
        for _, (_, _, mesh) in render_info.items():
            if mesh in mesh_meta:
                continue
            path = resolve_game_path(args.game_root, mesh)
            if path.suffix.lower() == ".dae" and path.is_file():
                try:
                    mesh_meta[mesh] = append_dae(path, stream)
                except Exception as error:
                    mesh_meta[mesh] = {"error": f"{type(error).__name__}: {error}"}
        proxy_first, proxy_count = append_vertices(stream, box_vertices())

    proxy_groups = [{"material": "proxy", "order": 0, "first": proxy_first, "count": proxy_count}]
    submeshes: list[tuple[int, int, str | None, int]] = []
    assets: list[tuple[int, int]] = []
    asset_cache: dict[tuple, int] = {}
    catalog_items = []
    failures = 0

    for uuid, (kind, item, source) in sorted(items.items()):
        profile = profile_for(kind, item, source)
        packed_color = color_rgba(item.get("color"))
        descriptors = []
        if kind == "blockList":
            descriptors.append((proxy_first, proxy_count, game_relative(item.get("dif")), packed_color))
        else:
            info = render_info.get(uuid)
            groups = None
            by_name: dict[str, str | None] = {}
            ordered: list[str | None] | None = None
            if info:
                _, lod, mesh = info
                meta = mesh_meta.get(mesh, {})
                groups = meta.get("groups")
                by_name, ordered = lod_materials(lod)
            if not groups:
                failures += 1
                groups = proxy_groups
            for group in groups:
                texture = texture_for_group(group, by_name, ordered)
                descriptors.append((int(group["first"]), int(group["count"]), game_relative(texture), packed_color))
        key = tuple(descriptors)
        asset_index = asset_cache.get(key)
        if asset_index is None:
            first = len(submeshes)
            submeshes.extend(descriptors)
            asset_index = len(assets)
            assets.append((first, len(descriptors)))
            asset_cache[key] = asset_index
        catalog_items.append((uuid, str(item.get("name") or uuid), CATALOG_PROFILES.index(profile), asset_index, packed_color,
                              source.name, PROFILE_LABELS[profile]))

    header = [
        "#pragma once",
        "// Generated from the installed Scrap Mechanic Chapter 2 shape databases and shipping meshes.",
        "namespace scrapvr::held_item_catalog {",
        "enum class Profile : unsigned char { " + ", ".join(CATALOG_PROFILES) + ", count };",
        "struct Submesh { unsigned long long first_vertex; unsigned int vertex_count; const wchar_t *texture; unsigned int rgba; };",
        "struct Asset { unsigned int first_submesh; unsigned int submesh_count; };",
        "struct Item { const char *uuid; const char *name; Profile profile; unsigned int asset; unsigned int tint; };",
        f"inline constexpr unsigned int packed_vertex_size = {PACKED_VERTEX_SIZE}u;",
        "inline constexpr Submesh submeshes[] = {",
    ]
    for first, count, texture, rgba in submeshes:
        header.append(f"    {{ {first}ull, {count}u, {cpp_wstring(texture)}, 0x{rgba:08x}u }},")
    header += ["};", "inline constexpr Asset assets[] = {"]
    for first, count in assets:
        header.append(f"    {{ {first}u, {count}u }},")
    header += ["};", "inline constexpr Item items[] = {"]
    for uuid, name, profile, asset, tint, _, _ in catalog_items:
        header.append(f"    {{ {cpp_string(uuid)}, {cpp_string(name)}, Profile::{CATALOG_PROFILES[profile]}, {asset}u, 0x{tint:08x}u }},")
    header += [
        "};",
        f"inline constexpr unsigned int item_count = {len(catalog_items)}u;",
        f"inline constexpr unsigned int asset_count = {len(assets)}u;",
        f"inline constexpr unsigned int submesh_count = {len(submeshes)}u;",
        "}",
    ]
    args.header.write_text("\n".join(header) + "\n", encoding="utf-8")
    with args.catalog_tsv.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("uuid\tname\tprofile\tshapeSet\n")
        for uuid, name, _, _, _, source, profile_label in catalog_items:
            stream.write("\t".join(value.replace("\t", " ").replace("\n", " ") for value in
                                   (uuid, name, profile_label, source)) + "\n")

    size = args.vertices.stat().st_size
    print(f"HELD_CATALOG_DONE items={len(catalog_items)} assets={len(assets)} submeshes={len(submeshes)} "
          f"bytes={size} proxy_items={failures}")


if __name__ == "__main__":
    main()
