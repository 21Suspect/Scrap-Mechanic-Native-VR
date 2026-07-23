#!/usr/bin/env python3
"""Generate static native VR tool meshes from installed Scrap Mechanic DAE files."""

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


NS = {"c": "http://www.collada.org/2005/11/COLLADASchema"}


def source_values(mesh):
    result = {}
    for source in mesh.findall("c:source", NS):
        accessor = source.find("c:technique_common/c:accessor", NS)
        stride = int(accessor.attrib.get("stride", "1"))
        values = [float(value) for value in source.find("c:float_array", NS).text.split()]
        result[source.attrib["id"]] = [
            tuple(values[index:index + stride]) for index in range(0, len(values), stride)
        ]
    return result


def cpp_float(value):
    text = f"{value:.7g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def identifier(value):
    return re.sub(r"[^a-zA-Z0-9_]", "_", value)


def extract(path):
    root = ET.parse(path).getroot()
    mesh = root.find(".//c:library_geometries/c:geometry/c:mesh", NS)
    sources = source_values(mesh)
    vertices = mesh.find("c:vertices", NS)
    vertex_sources = {
        item.attrib["semantic"]: item.attrib["source"].lstrip("#")
        for item in vertices.findall("c:input", NS)
    }
    groups = []
    for triangles in mesh.findall("c:triangles", NS):
        inputs = []
        for item in triangles.findall("c:input", NS):
            semantic = item.attrib["semantic"]
            source = item.attrib["source"].lstrip("#")
            if semantic == "VERTEX":
                # Collada permits POSITION, NORMAL and TEXCOORD to be bundled
                # into one <vertices> stream. The weld tool uses that form,
                # while most Scrap Mechanic tools keep only POSITION there.
                # Every bundled semantic shares the VERTEX corner index.
                inputs.extend(
                    (vertex_semantic, int(item.attrib["offset"]), vertex_source)
                    for vertex_semantic, vertex_source in vertex_sources.items()
                )
            else:
                inputs.append((semantic, int(item.attrib["offset"]), source))
        stride = max(offset for _, offset, _ in inputs) + 1
        packed = [int(value) for value in triangles.find("c:p", NS).text.split()]
        corners = []
        for cursor in range(0, len(packed), stride):
            record = packed[cursor:cursor + stride]
            corner = {
                semantic: sources[source][record[offset]]
                for semantic, offset, source in inputs
            }
            corners.append(corner)
        emitted = []
        for triangle_start in range(0, len(corners), 3):
            triangle = corners[triangle_start:triangle_start + 3]
            a, b, c = (item["POSITION"] for item in triangle)
            ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            face = (
                ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0],
            )
            magnitude = sum(value * value for value in face) ** 0.5 or 1.0
            face = tuple(value / magnitude for value in face)
            for corner in triangle:
                position = corner["POSITION"]
                normal = corner.get("NORMAL", face)
                uv = corner.get("TEXCOORD", (0.0, 0.0))
                emitted.append((
                    position[0], position[1], position[2],
                    normal[0], normal[1], normal[2],
                    uv[0], 1.0 - uv[1],
                ))
        groups.append((triangles.attrib.get("material", "material"), emitted))
    return groups


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_vr_tools.py GAME_ROOT OUTPUT.hpp")
    game_root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    tools = {
        "hammer": [("main", game_root / "Data/Character/Char_Tools/Char_sledgehammer/char_sledgehammer.dae")],
        "connect": [("main", game_root / "Data/Character/Char_Tools/Char_connecttool/char_connecttool.dae")],
        "paint": [("main", game_root / "Data/Character/Char_Tools/Char_painttool/char_painttool.dae")],
        "weld": [("main", game_root / "Data/Character/Char_Tools/Char_weldtool/char_weldtool.dae")],
        "gunshared": [
            ("base", game_root / "Data/Character/Char_Tools/Char_spudgun/Base/char_spudgun_base_basic_geo.dae"),
            ("sight", game_root / "Data/Character/Char_Tools/Char_spudgun/Sight/Sight_basic/char_spudgun_sight_basic_geo.dae"),
            ("stock", game_root / "Data/Character/Char_Tools/Char_spudgun/Stock/Stock_broom/char_spudgun_stock_broom_geo.dae"),
            ("tank", game_root / "Data/Character/Char_Tools/Char_spudgun/Tank/Tank_basic/char_spudgun_tank_basic_geo.dae"),
        ],
        "rifle": [
            ("barrel", game_root / "Data/Character/Char_Tools/Char_spudgun/Barrel/Barrel_basic/char_spudgun_barrel_basic_geo.dae"),
        ],
        "shotgun": [
            ("barrel", game_root / "Data/Character/Char_Tools/Char_spudgun/Barrel/Barrel_frier/char_spudgun_barrel_frier_geo.dae"),
        ],
        "gatling": [
            ("barrel", game_root / "Data/Character/Char_Tools/Char_spudgun/Barrel/Barrel_spinner/char_spudgun_barrel_spinner_geo.dae"),
        ],
    }
    lines = [
        "#pragma once",
        "// Generated from the installed Scrap Mechanic connect, paint, and weld DAE meshes.",
        "namespace scrapvr::native_tool_asset {",
        "struct Vertex { float px, py, pz, nx, ny, nz, u, v; };",
    ]
    for tool, parts in tools.items():
        for part, path in parts:
            groups = extract(path)
            for group_index, (material, vertices) in enumerate(groups):
                name = f"{tool}_{part}_{group_index}_{identifier(material)}"
                lines.append(f"inline constexpr Vertex {name}_vertices[] = {{")
                lines.extend(
                    "    { " + ", ".join(cpp_float(component) for component in vertex) + " },"
                    for vertex in vertices
                )
                lines.append("};")
                lines.append(f"inline constexpr unsigned int {name}_vertex_count = {len(vertices)}u;")
                print(f"{tool} {part} group {group_index} ({material}): {len(vertices)} vertices")
    lines.append("}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
