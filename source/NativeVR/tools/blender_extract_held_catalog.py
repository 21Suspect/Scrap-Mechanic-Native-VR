#!/usr/bin/env python3
"""Blender-side FBX extractor for the generated held-item catalog.

Run by generate_full_held_item_catalog.py.  Each shipping mesh is normalized
to a two-unit longest axis and written as the native renderer's interleaved
position/normal/UV vertex format.  Geometry is split by material so the
regular generator can attach the diffuse texture declared by each .rend file.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
import struct
import sys
import traceback

import bpy
from mathutils import Vector


# A held item occupies only a small part of each eye.  Shipping environment
# meshes can contain over a million triangle vertices and are not appropriate
# for this pass; those receive the generator's bounded proxy instead.
MAX_HELD_VERTICES = 100_000
PACKED_VERTEX_FORMAT = "<3f3h2e"


def clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
        for value in list(collection):
            collection.remove(value)


def transformed_position(obj, vertex) -> Vector:
    return obj.matrix_world @ vertex.co


def material_name(mesh, index: int) -> str:
    if 0 <= index < len(mesh.materials) and mesh.materials[index] is not None:
        return mesh.materials[index].name
    return f"material_{index}"


def write_vertex(stream, vertex: tuple[float, ...]) -> None:
    normal = [max(-1.0, min(1.0, float(vertex[index]))) for index in range(3, 6)]
    uv = [max(-65504.0, min(65504.0, float(vertex[index]))) for index in range(6, 8)]
    stream.write(struct.pack(
        PACKED_VERTEX_FORMAT,
        float(vertex[0]), float(vertex[1]), float(vertex[2]),
        *(int(round(value * 32767.0)) for value in normal),
        *uv,
    ))


def extract_mesh(path: Path, stream, first_vertex: int) -> tuple[list[dict], int, dict]:
    clear_scene()
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=False, ignore_leaf_bones=True)
    objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not objects:
        raise RuntimeError("FBX contains no mesh objects")

    positions = [transformed_position(obj, vertex) for obj in objects for vertex in obj.data.vertices]
    minimum = Vector((min(value.x for value in positions), min(value.y for value in positions), min(value.z for value in positions)))
    maximum = Vector((max(value.x for value in positions), max(value.y for value in positions), max(value.z for value in positions)))
    center = (minimum + maximum) * 0.5
    dimensions = maximum - minimum
    longest = max(dimensions.x, dimensions.y, dimensions.z)
    if not math.isfinite(longest) or longest < 1.0e-7:
        raise RuntimeError("FBX mesh has invalid bounds")
    normalize = 2.0 / longest

    grouped: dict[str, list[tuple[float, ...]]] = {}
    ordered_materials: list[str] = []
    for obj in objects:
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None
        for triangle in mesh.loop_triangles:
            name = material_name(mesh, triangle.material_index)
            if name not in grouped:
                grouped[name] = []
                ordered_materials.append(name)
            loops = list(triangle.loops)
            points = [transformed_position(obj, mesh.vertices[mesh.loops[index].vertex_index]) for index in loops]
            edge_a, edge_b = points[1] - points[0], points[2] - points[0]
            normal = edge_a.cross(edge_b)
            if normal.length_squared < 1.0e-14:
                continue
            normal.normalize()
            for loop_index, point in zip(loops, points):
                uv = uv_layer[loop_index].uv if uv_layer else (0.0, 0.0)
                local = (point - center) * normalize
                grouped[name].append((
                    local.x, local.y, local.z,
                    normal.x, normal.y, normal.z,
                    float(uv[0]), 1.0 - float(uv[1]),
                ))

    groups = []
    cursor = first_vertex
    total_vertices = sum(len(vertices) for vertices in grouped.values())
    if total_vertices > MAX_HELD_VERTICES:
        raise RuntimeError(
            f"FBX exceeds held-item budget ({total_vertices} > {MAX_HELD_VERTICES} vertices)"
        )
    for order, name in enumerate(ordered_materials):
        vertices = grouped[name]
        if not vertices:
            continue
        for vertex in vertices:
            write_vertex(stream, vertex)
        groups.append({"material": name, "order": order, "first": cursor, "count": len(vertices)})
        cursor += len(vertices)
    if not groups:
        raise RuntimeError("FBX produced no triangles")
    bounds = {
        "sourceMin": [minimum.x, minimum.y, minimum.z],
        "sourceMax": [maximum.x, maximum.y, maximum.z],
        "normalization": normalize,
    }
    return groups, cursor, bounds


def main() -> None:
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if len(args) != 3:
        raise SystemExit("usage: blender --background --python blender_extract_held_catalog.py -- TASKS.json VERTICES.bin META.json")
    tasks_path, vertices_path, metadata_path = map(Path, args)
    tasks = json.loads(tasks_path.read_text(encoding="utf-8"))
    metadata: dict[str, dict] = {}
    first_vertex = 0
    with vertices_path.open("wb") as stream:
        for index, task in enumerate(tasks, 1):
            key, path = task["key"], Path(task["path"])
            try:
                groups, first_vertex, bounds = extract_mesh(path, stream, first_vertex)
                metadata[key] = {"groups": groups, "bounds": bounds}
            except Exception as error:
                metadata[key] = {"error": f"{type(error).__name__}: {error}"}
                traceback.print_exc()
            if index == 1 or index % 25 == 0 or index == len(tasks):
                print(f"HELD_CATALOG_EXTRACT {index}/{len(tasks)} vertices={first_vertex} failures={sum('error' in value for value in metadata.values())}", flush=True)
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"HELD_CATALOG_EXTRACT_DONE meshes={len(tasks)} vertices={first_vertex}", flush=True)


if __name__ == "__main__":
    main()
