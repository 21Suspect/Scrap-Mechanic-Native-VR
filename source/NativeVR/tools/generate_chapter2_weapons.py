#!/usr/bin/env python3
"""Generate native VR meshes for Scrap Mechanic 1.0 Chapter 2 gun barrels."""

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


def matrix_multiply(a, b):
    return [[sum(a[row][k] * b[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def node_matrix(node):
    values = [float(value) for value in node.find("c:matrix", NS).text.split()]
    return [values[index:index + 4] for index in range(0, 16, 4)]


def joint_pivot(root, name):
    nodes = {node.attrib["id"]: node for node in root.findall(".//c:node[@type='JOINT']", NS)}
    parents = {child: parent for parent in root.iter() for child in parent}
    chain = []
    node = nodes[name]
    while node is not None and node.attrib.get("type") == "JOINT":
        chain.append(node_matrix(node))
        node = parents.get(node)
    result = [[1.0 if row == column else 0.0 for column in range(4)] for row in range(4)]
    for matrix in reversed(chain):
        result = matrix_multiply(result, matrix)
    return result[0][3], result[1][3], result[2][3]


def dominant_joints(root, geometry_id):
    for controller in root.findall(".//c:library_controllers/c:controller", NS):
        skin = controller.find("c:skin", NS)
        if skin is None or skin.attrib.get("source", "").lstrip("#") != geometry_id:
            continue
        sources = {source.attrib["id"]: source for source in skin.findall("c:source", NS)}
        joint_source = skin.find("c:joints/c:input[@semantic='JOINT']", NS).attrib["source"].lstrip("#")
        names = sources[joint_source].find("c:Name_array", NS).text.split()
        vertex_weights = skin.find("c:vertex_weights", NS)
        inputs = {
            item.attrib["semantic"]: (int(item.attrib["offset"]), item.attrib["source"].lstrip("#"))
            for item in vertex_weights.findall("c:input", NS)
        }
        stride = max(offset for offset, _ in inputs.values()) + 1
        joint_offset = inputs["JOINT"][0]
        weight_offset, weight_source = inputs["WEIGHT"]
        weights = [float(value) for value in sources[weight_source].find("c:float_array", NS).text.split()]
        counts = [int(value) for value in vertex_weights.find("c:vcount", NS).text.split()]
        packed = [int(value) for value in vertex_weights.find("c:v", NS).text.split()]
        result = []
        cursor = 0
        for count in counts:
            influences = []
            for _ in range(count):
                record = packed[cursor:cursor + stride]
                cursor += stride
                influences.append((names[record[joint_offset]], weights[record[weight_offset]]))
            result.append(max(influences, key=lambda item: item[1])[0] if influences else "")
        return result
    return None


def primitive_corners(mesh, primitive, sources, vertex_sources):
    inputs = []
    for item in primitive.findall("c:input", NS):
        semantic = item.attrib["semantic"]
        source = item.attrib["source"].lstrip("#")
        if semantic == "VERTEX":
            inputs.extend(
                (vertex_semantic, int(item.attrib["offset"]), vertex_source)
                for vertex_semantic, vertex_source in vertex_sources.items()
            )
        elif semantic != "TEXCOORD" or item.attrib.get("set", "0") == "0":
            inputs.append((semantic, int(item.attrib["offset"]), source))
    stride = max(offset for _, offset, _ in inputs) + 1
    packed = [int(value) for value in primitive.find("c:p", NS).text.split()]
    corners = []
    for cursor in range(0, len(packed), stride):
        record = packed[cursor:cursor + stride]
        corner = {
            semantic: sources[source][record[offset]]
            for semantic, offset, source in inputs
        }
        corner["POSITION_INDEX"] = next(
            record[offset] for semantic, offset, _ in inputs if semantic == "POSITION"
        )
        corners.append(corner)
    return corners


def emit_triangle(triangle):
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
    emitted = []
    for corner in triangle:
        position = corner["POSITION"]
        normal = corner.get("NORMAL", face)
        uv = corner.get("TEXCOORD", (0.0, 0.0))
        emitted.append((
            position[0], position[1], position[2],
            normal[0], normal[1], normal[2],
            uv[0], 1.0 - uv[1],
        ))
    return emitted


def extract(path, split_clay=False):
    root = ET.parse(path).getroot()
    groups = []
    for geometry_index, geometry in enumerate(root.findall(".//c:library_geometries/c:geometry", NS)):
        mesh = geometry.find("c:mesh", NS)
        sources = source_values(mesh)
        vertices = mesh.find("c:vertices", NS)
        vertex_sources = {
            item.attrib["semantic"]: item.attrib["source"].lstrip("#")
            for item in vertices.findall("c:input", NS)
        }
        dominant = dominant_joints(root, geometry.attrib["id"]) if split_clay else None
        for primitive in list(mesh):
            kind = primitive.tag.rsplit("}", 1)[-1]
            if kind not in ("triangles", "polylist"):
                continue
            corners = primitive_corners(mesh, primitive, sources, vertex_sources)
            polygons = []
            if kind == "triangles":
                polygons = [corners[index:index + 3] for index in range(0, len(corners), 3)]
            else:
                counts = [int(value) for value in primitive.find("c:vcount", NS).text.split()]
                cursor = 0
                for count in counts:
                    polygon = corners[cursor:cursor + count]
                    cursor += count
                    for index in range(1, count - 1):
                        polygons.append((polygon[0], polygon[index], polygon[index + 1]))
                if cursor != len(corners):
                    raise ValueError(f"polylist corner count mismatch in {path}")
            emitted = {}
            for triangle in polygons:
                part = "static"
                if split_clay and geometry_index == 0 and dominant:
                    labels = []
                    for corner in triangle:
                        joint = dominant[corner["POSITION_INDEX"]]
                        if joint == "jnt_container":
                            labels.append("container")
                        elif joint in {"jnt_wheel", "jnt_bucket01", "jnt_bucket02", "jnt_bucket03", "jnt_bucket04", "jnt_bucket05"}:
                            labels.append("wheel")
                        else:
                            labels.append("static")
                    if len(set(labels)) != 1:
                        raise RuntimeError(f"mixed clay animation ownership in {path}: {labels}")
                    part = labels[0]
                emitted.setdefault(part, []).extend(emit_triangle(triangle))
            for part, vertices_out in emitted.items():
                groups.append((geometry_index, primitive.attrib.get("material", "material"), part, vertices_out))
    return root, groups


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_chapter2_weapons.py GAME_ROOT OUTPUT.hpp")
    game_root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    assets = {
        "scrap": game_root / "Data/Character/Char_Tools/Char_spudgun/Barrel/Barrel_scrap/char_spudgun_barrel_scrap.dae",
        "launcher": game_root / "Data/Character/Char_Tools/Char_spudgun/Barrel/Barrel_launcher/char_spudgun_barrel_launcher.dae",
        "clay": game_root / "Data/Character/Char_Tools/Char_claygun/char_claygun.dae",
    }
    lines = [
        "#pragma once",
        "// Generated from installed Scrap Mechanic 1.0 Chapter 2 DAE meshes.",
        "namespace scrapvr::chapter2_tool_asset {",
        "using Vertex = native_tool_asset::Vertex;",
    ]
    for weapon, path in assets.items():
        root, groups = extract(path, split_clay=weapon == "clay")
        for group_index, (geometry_index, material, part, vertices) in enumerate(groups):
            if weapon != "clay":
                name = f"{weapon}_barrel_{group_index}_{identifier(material)}"
            elif geometry_index == 1:
                name = "clay_grip"
            elif material == "m_claygun" and part == "static":
                name = "clay_body"
            elif material == "m_claygun" and part == "wheel":
                name = "clay_wheel"
            elif material == "m_clay" and part == "container":
                name = "clay_container_fill"
            elif material == "m_glass" and part == "container":
                name = "clay_container_glass"
            else:
                raise RuntimeError(f"unclassified clay group geometry={geometry_index} material={material} part={part}")
            lines.append(f"inline constexpr Vertex {name}_vertices[] = {{")
            lines.extend(
                "    { " + ", ".join(cpp_float(component) for component in vertex) + " },"
                for vertex in vertices
            )
            lines.append("};")
            lines.append(f"inline constexpr unsigned int {name}_vertex_count = {len(vertices)}u;")
            print(f"{weapon} group {group_index} ({material}, {part}): {len(vertices)} vertices")
        if weapon == "clay":
            for joint, name in (("jnt_container", "clay_container_pivot"), ("jnt_wheel", "clay_wheel_pivot")):
                pivot = joint_pivot(root, joint)
                lines.append(f"inline constexpr float {name}[3] = {{ " +
                             ", ".join(cpp_float(value) for value in pivot) + " };")
    lines.append("}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
