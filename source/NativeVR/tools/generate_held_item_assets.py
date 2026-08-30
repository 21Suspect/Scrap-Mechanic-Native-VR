#!/usr/bin/env python3
"""Generate direct-stereo VR meshes for Chapter 2 held items.

The installed game remains the source of truth.  This script extracts the
shipping Collada meshes and diffuse texture paths into a deterministic C++
header.  Small proxy meshes are generated only where the game ships an FBX
without an equivalent Collada source (seed, key, residue and arbitrary carry).
"""

from dataclasses import dataclass
import json
from pathlib import Path
import sys

from generate_chapter2_weapons import cpp_float, extract, identifier


@dataclass(frozen=True)
class Asset:
    name: str
    dae: str
    rend: str


ASSETS = (
    Asset("lift", "Data/Character/Char_Tools/Char_liftremote/char_liftremote.dae",
          "Data/Character/Char_Tools/Char_liftremote/char_liftremote.rend"),
    Asset("handbook", "Data/Character/Char_Tools/Char_handbook/char_handbook.dae",
          "Data/Character/Char_Tools/Char_handbook/char_handbook.rend"),
    Asset("bucket_source", "Survival/Character/Char_bucket/char_bucket.dae",
          "Survival/Character/Char_bucket/char_bucket_empty.rend"),
    Asset("glowstick", "Survival/Character/Char_Glowstick/char_glowstick.dae",
          "Survival/Character/Char_Glowstick/char_glowstick.rend"),
    Asset("cornade", "Survival/Character/Char_Cornade/char_cornade.dae",
          "Survival/Character/Char_Cornade/char_cornade.rend"),
    Asset("loose_clay", "Survival/Character/Char_Tools/Char_clay/char_claytool.dae",
          "Survival/Character/Char_Tools/Char_clay/char_clay.rend"),
    Asset("extinguisher", "Survival/Character/Char_Tools/Char_extinguisher/char_extinguisher.dae",
          "Survival/Character/Char_Tools/Char_extinguisher/char_extinguisher.rend"),
    Asset("fertilizer", "Survival/Character/Char_Tools/Char_fertilizer/char_fertilizer.dae",
          "Survival/Character/Char_Tools/Char_fertilizer/char_fertilizer.rend"),
    Asset("feeder", "Survival/Character/Char_Tools/Char_longsandwich/char_longsandwich.dae",
          "Survival/Character/Char_Tools/Char_longsandwich/char_longsandwich.rend"),
    Asset("soilbag", "Survival/Character/Char_Tools/Char_soilbag/char_soilbag.dae",
          "Survival/Character/Char_Tools/Char_soilbag/char_soilbag.rend"),
    Asset("logbook", "Survival/Character/Char_Tools/Char_logbook/char_logbook.dae",
          "Survival/Character/Char_Tools/Char_logbook/char_logbook.rend"),
    Asset("food_sunshake", "Survival/Character/Char_Tools/Char_eattool/char_eattool_sunshake.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_sunshake.rend"),
    Asset("food_milk", "Survival/Character/Char_Tools/Char_eattool/char_eattool_milk.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_milk.rend"),
    Asset("food_carrotburger", "Survival/Character/Char_Tools/Char_carrotburger/char_carrotburger.dae",
          "Survival/Character/Char_Tools/Char_carrotburger/char_carrotburger.rend"),
    Asset("food_pizzaburger", "Survival/Character/Char_Tools/Char_pizzaburger/char_pizzaburger.dae",
          "Survival/Character/Char_Tools/Char_pizzaburger/char_pizzaburger.rend"),
    Asset("food_banana", "Survival/Character/Char_Tools/Char_eattool/char_eattool_banana.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_banana.rend"),
    Asset("food_blueberry", "Survival/Character/Char_Tools/Char_eattool/char_eattool_blueberry.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_blueberry.rend"),
    Asset("food_orange", "Survival/Character/Char_Tools/Char_eattool/char_eattool_orange.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_orange.rend"),
    Asset("food_pineapple", "Survival/Character/Char_Tools/Char_eattool/char_eattool_pineapple.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_pineapple.rend"),
    Asset("food_carrot", "Survival/Character/Char_Tools/Char_eattool/char_eattool_carrot.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_carrot.rend"),
    Asset("food_redbeet", "Survival/Character/Char_Tools/Char_eattool/char_eattool_redbeet.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_redbeet.rend"),
    Asset("food_tomato", "Survival/Character/Char_Tools/Char_eattool/char_eattool_tomato.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_tomato.rend"),
    Asset("food_broccoli", "Survival/Character/Char_Tools/Char_eattool/char_eattool_broccoli.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_brococoli.rend"),
    Asset("food_corn", "Survival/Character/Char_Tools/Char_eattool/char_eattool_corn.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_corn.rend"),
    Asset("food_tea", "Survival/Character/Char_Tools/Char_eattool/char_eattool_tea.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_tea.rend"),
    Asset("food_chili", "Survival/Character/Char_Tools/Char_eattool/char_eattool_chili.dae",
          "Survival/Character/Char_Tools/Char_eattool/char_eattool_chili.rend"),
)


def game_relative(path: str) -> str:
    return path.replace("$GAME_DATA/", "Data/").replace("$SURVIVAL_DATA/", "Survival/")


def diffuse_entries(game_root: Path, relative: str):
    data = json.loads((game_root / relative).read_text(encoding="utf-8-sig"))
    lod = data["lodList"][0]
    if "subMeshMap" in lod:
        result = {}
        for material, value in lod["subMeshMap"].items():
            textures = value.get("textures", {})
            diffuse = textures.get("diffuse")
            if diffuse is None and value.get("textureList"):
                diffuse = value["textureList"][0]
            result[material] = game_relative(diffuse) if diffuse else None
        return result, None
    ordered = []
    for value in lod.get("subMeshList", []):
        textures = value.get("textures", {})
        diffuse = textures.get("diffuse")
        if diffuse is None and value.get("textureList"):
            diffuse = value["textureList"][0]
        ordered.append(game_relative(diffuse) if diffuse else None)
    return {}, ordered


def vertex(position, normal, uv):
    return (*position, *normal, *uv)


def box(size=(1.0, 1.0, 1.0), center=(0.0, 0.0, 0.0)):
    sx, sy, sz = (value * 0.5 for value in size)
    cx, cy, cz = center
    faces = (
        ((1, 0, 0), ((sx, -sy, -sz), (sx, sy, -sz), (sx, sy, sz), (sx, -sy, sz))),
        ((-1, 0, 0), ((-sx, sy, -sz), (-sx, -sy, -sz), (-sx, -sy, sz), (-sx, sy, sz))),
        ((0, 1, 0), ((-sx, sy, -sz), (sx, sy, -sz), (sx, sy, sz), (-sx, sy, sz))),
        ((0, -1, 0), ((sx, -sy, -sz), (-sx, -sy, -sz), (-sx, -sy, sz), (sx, -sy, sz))),
        ((0, 0, 1), ((-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz))),
        ((0, 0, -1), ((-sx, sy, -sz), (sx, sy, -sz), (sx, -sy, -sz), (-sx, -sy, -sz))),
    )
    result = []
    uvs = ((0, 1), (1, 1), (1, 0), (0, 0))
    for normal, corners in faces:
        corners = [tuple(value + offset for value, offset in zip(point, center)) for point in corners]
        for index in (0, 1, 2, 0, 2, 3):
            result.append(vertex(corners[index], normal, uvs[index]))
    return result


def cylinder(radius=0.5, length=1.0, segments=16):
    import math
    result = []
    y0, y1 = -length * 0.5, length * 0.5
    for index in range(segments):
        a0, a1 = 2 * math.pi * index / segments, 2 * math.pi * (index + 1) / segments
        x0, z0, x1, z1 = radius * math.cos(a0), radius * math.sin(a0), radius * math.cos(a1), radius * math.sin(a1)
        n0, n1 = (math.cos(a0), 0, math.sin(a0)), (math.cos(a1), 0, math.sin(a1))
        result.extend((
            vertex((x0, y0, z0), n0, (index / segments, 1)),
            vertex((x1, y0, z1), n1, ((index + 1) / segments, 1)),
            vertex((x1, y1, z1), n1, ((index + 1) / segments, 0)),
            vertex((x0, y0, z0), n0, (index / segments, 1)),
            vertex((x1, y1, z1), n1, ((index + 1) / segments, 0)),
            vertex((x0, y1, z0), n0, (index / segments, 0)),
            vertex((0, y0, 0), (0, -1, 0), (0.5, 0.5)),
            vertex((x1, y0, z1), (0, -1, 0), (0.5 + x1, 0.5 + z1)),
            vertex((x0, y0, z0), (0, -1, 0), (0.5 + x0, 0.5 + z0)),
            vertex((0, y1, 0), (0, 1, 0), (0.5, 0.5)),
            vertex((x0, y1, z0), (0, 1, 0), (0.5 + x0, 0.5 + z0)),
            vertex((x1, y1, z1), (0, 1, 0), (0.5 + x1, 0.5 + z1)),
        ))
    return result


def rock():
    import math
    rings = (
        (0.0, -0.72, 0.12),
        (0.0, -0.35, 0.72),
        (0.0, 0.28, 0.88),
        (0.0, 0.72, 0.18),
    )
    segments = 9
    points = []
    for ring, (unused, y, radius) in enumerate(rings):
        row = []
        for index in range(segments):
            angle = 2 * math.pi * index / segments
            wobble = 1.0 + 0.16 * math.sin(index * 2.17 + ring * 1.31)
            row.append((radius * wobble * math.cos(angle), y, radius * wobble * math.sin(angle)))
        points.append(row)
    result = []
    for ring in range(len(points) - 1):
        for index in range(segments):
            nxt = (index + 1) % segments
            quad = (points[ring][index], points[ring][nxt], points[ring + 1][nxt], points[ring + 1][index])
            for triangle in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                ax, ay, az = triangle[0]; bx, by, bz = triangle[1]; cx, cy, cz = triangle[2]
                ab, ac = (bx-ax, by-ay, bz-az), (cx-ax, cy-ay, cz-az)
                normal = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
                mag = math.sqrt(sum(value*value for value in normal)) or 1.0
                normal = tuple(value/mag for value in normal)
                result.extend(vertex(point, normal, (0.5 + point[0] * 0.4, 0.5 + point[2] * 0.4)) for point in triangle)
    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_held_item_assets.py GAME_ROOT OUTPUT.hpp")
    game_root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    lines = [
        "#pragma once",
        "// Generated from installed Scrap Mechanic 1.0 Chapter 2 assets.",
        "namespace scrapvr::held_item_asset {",
        "using Vertex = native_tool_asset::Vertex;",
        "struct MeshSource { const Vertex *vertices; unsigned int count; const wchar_t *texture; unsigned int rgba; };",
        "struct MeshRange { unsigned int first; unsigned int count; };",
    ]
    sources = []
    ranges = {}

    def add_source(name, vertices, texture=None, rgba=0xffffffff):
        array_name = identifier(name)
        lines.append(f"inline constexpr Vertex {array_name}_vertices[] = {{")
        lines.extend("    { " + ", ".join(cpp_float(value) for value in item) + " }," for item in vertices)
        lines.append("};")
        sources.append((array_name, len(vertices), texture, rgba))

    for asset in ASSETS:
        _, groups = extract(game_root / asset.dae)
        by_material, ordered = diffuse_entries(game_root, asset.rend)
        first = len(sources)
        bucket_water_vertices = None
        for group_index, (_, material, _, vertices) in enumerate(groups):
            texture = by_material.get(material)
            if texture is None and ordered is not None and group_index < len(ordered):
                texture = ordered[group_index]
            if asset.name == "bucket_source" and material == "water_mat":
                bucket_water_vertices = vertices
                continue
            add_source(f"{asset.name}_{group_index}_{material}", vertices, texture)
        if asset.name == "bucket_source":
            ranges["bucket"] = (first, len(sources) - first)
            if bucket_water_vertices is None:
                raise RuntimeError("bucket liquid submesh was not found")
            for name, color in (("bucket_water", 0xd9e8a14b), ("bucket_oil", 0xf0522a16), ("bucket_chemical", 0xe13fc549)):
                liquid_first = len(sources)
                add_source(name, bucket_water_vertices, None, color)
                ranges[name] = (liquid_first, 1)
        else:
            ranges[asset.name] = (first, len(sources) - first)
        print(f"{asset.name}: {len(groups)} submeshes")

    proxies = (
        ("planter", box((1.05, 0.26, 0.82)), "Survival/Character/Char_Tools/Char_seed/char_seed_dif.tga", 0xffffffff),
        ("keycard", box((1.22, 0.12, 0.76)), "Survival/Objects/Textures/survivalobject/obj_survivalobject_keycard_dif.tga", 0xffffffff),
        ("powercore", cylinder(0.48, 1.15, 18), "Survival/Objects/Textures/survivalobject/obj_survivalobject_powercore_dif.tga", 0xffffffff),
        ("resource", rock(), None, 0xff795148),
        ("carry", box((1.55, 1.15, 1.05)), None, 0xff2f87d8),
    )
    for name, vertices, texture, color in proxies:
        first = len(sources)
        add_source(name, vertices, texture, color)
        ranges[name] = (first, 1)

    lines.append("inline constexpr MeshSource meshes[] = {")
    for name, count, texture, rgba in sources:
        texture_cpp = "nullptr" if texture is None else 'L"' + texture.replace("/", "\\\\") + '"'
        lines.append(f"    {{ {name}_vertices, {count}u, {texture_cpp}, 0x{rgba:08x}u }},")
    lines.append("};")
    lines.append(f"inline constexpr unsigned int mesh_count = {len(sources)}u;")
    for name, (first, count) in ranges.items():
        lines.append(f"inline constexpr MeshRange {identifier(name)} = {{ {first}u, {count}u }};")
    lines.append("}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {output} with {len(sources)} draw resources")


if __name__ == "__main__":
    main()
