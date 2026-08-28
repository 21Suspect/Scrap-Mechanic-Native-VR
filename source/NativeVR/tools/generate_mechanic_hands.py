#!/usr/bin/env python3
"""Generate the native VR hand mesh from the installed Scrap Mechanic 1.0 glove DAE."""

from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET


NS = {"c": "http://www.collada.org/2005/11/COLLADASchema"}


def read_tga(path):
    """Read the installed uncompressed/RLE true-colour TGA without third-party modules."""
    data = path.read_bytes()
    if len(data) < 18:
        raise RuntimeError(f"invalid glove texture: {path}")
    identifier_length, colour_map_type, image_type = struct.unpack_from("<BBB", data, 0)
    width, height, bits_per_pixel, descriptor = struct.unpack_from("<HHBB", data, 12)
    if colour_map_type != 0 or image_type not in (2, 10) or bits_per_pixel not in (24, 32):
        raise RuntimeError(f"unsupported glove TGA format: type={image_type} bpp={bits_per_pixel}")
    bytes_per_pixel = bits_per_pixel // 8
    cursor = 18 + identifier_length
    pixels = []

    def read_pixel():
        nonlocal cursor
        if cursor + bytes_per_pixel > len(data):
            raise RuntimeError("truncated glove TGA pixel data")
        blue, green, red = data[cursor:cursor + 3]
        cursor += bytes_per_pixel
        return red, green, blue

    if image_type == 2:
        pixels = [read_pixel() for _ in range(width * height)]
    else:
        while len(pixels) < width * height:
            if cursor >= len(data):
                raise RuntimeError("truncated glove TGA RLE packet")
            packet = data[cursor]
            cursor += 1
            count = (packet & 0x7f) + 1
            if packet & 0x80:
                pixels.extend([read_pixel()] * count)
            else:
                pixels.extend(read_pixel() for _ in range(count))
        pixels = pixels[:width * height]
    return width, height, descriptor, pixels


def texture_pixel(texture, uv):
    width, height, descriptor, pixels = texture
    u, v = uv[:2]
    x = min(width - 1, int((u % 1.0) * width))
    y = min(height - 1, int((v % 1.0) * height))
    if descriptor & 0x10:
        x = width - 1 - x
    if descriptor & 0x20:
        y = height - 1 - y
    return pixels[y * width + x]


def is_exposed_skin(texture, uv):
    # The ship-mechanic atlas contains a salmon skin island and olive/yellow
    # glove islands. Colour classification is more reliable than bone names:
    # the glove cuff and exposed forearm are both weighted to forearmroll.
    red, green, blue = texture_pixel(texture, uv)
    return red - green > 38 and blue > 35 and red - blue > 38


def floats(source):
    return [float(value) for value in source.find("c:float_array", NS).text.split()]


def tuples(source):
    accessor = source.find("c:technique_common/c:accessor", NS)
    stride = int(accessor.attrib.get("stride", "1"))
    values = floats(source)
    return [tuple(values[index:index + stride]) for index in range(0, len(values), stride)]


def cpp_float(value):
    text = f"{value:.7g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def matrix_multiply(a, b):
    return [[sum(a[row][k] * b[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def node_matrix(node):
    values = [float(value) for value in node.find("c:matrix", NS).text.split()]
    return [values[index:index + 4] for index in range(0, 16, 4)]


def primitive_triangles(mesh, sources, vertex_sources):
    result = []
    for primitive in list(mesh):
        kind = primitive.tag.rsplit("}", 1)[-1]
        if kind not in ("triangles", "polylist"):
            continue
        inputs = []
        for item in primitive.findall("c:input", NS):
            semantic = item.attrib["semantic"]
            source = item.attrib["source"].lstrip("#")
            offset = int(item.attrib["offset"])
            if semantic == "VERTEX":
                inputs.extend((name, offset, vertex_source) for name, vertex_source in vertex_sources.items())
            elif semantic != "TEXCOORD" or item.attrib.get("set", "0") == "0":
                inputs.append((semantic, offset, source))
        stride = max(offset for _, offset, _ in inputs) + 1
        packed = [int(value) for value in primitive.find("c:p", NS).text.split()]
        corners = []
        for cursor in range(0, len(packed), stride):
            record = packed[cursor:cursor + stride]
            corner = {}
            for semantic, offset, source in inputs:
                corner[semantic] = sources[source][record[offset]]
                if semantic == "POSITION":
                    corner["POSITION_INDEX"] = record[offset]
            corners.append(corner)
        if kind == "triangles":
            result.extend(tuple(corners[index:index + 3]) for index in range(0, len(corners), 3))
        else:
            counts = [int(value) for value in primitive.find("c:vcount", NS).text.split()]
            cursor = 0
            for count in counts:
                polygon = corners[cursor:cursor + count]
                cursor += count
                result.extend((polygon[0], polygon[index], polygon[index + 1]) for index in range(1, count - 1))
            if cursor != len(corners):
                raise RuntimeError("glove polylist corner count mismatch")
    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_mechanic_hands.py INPUT.dae OUTPUT.hpp")

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    diffuse_path = input_path.with_name(input_path.stem + "_dif.tga")
    if not diffuse_path.is_file():
        raise RuntimeError(f"matching glove diffuse texture was not found: {diffuse_path}")
    diffuse_texture = read_tga(diffuse_path)
    root = ET.parse(input_path).getroot()
    mesh = root.find(".//c:library_geometries/c:geometry/c:mesh", NS)
    if mesh is None:
        raise RuntimeError("glove geometry was not found")

    controller = root.find(".//c:library_controllers/c:controller/c:skin", NS)
    if controller is None:
        raise RuntimeError("glove skin controller was not found")
    controller_sources = {source.attrib["id"]: source for source in controller.findall("c:source", NS)}
    joint_source = controller.find("c:joints/c:input[@semantic='JOINT']", NS).attrib["source"].lstrip("#")
    joint_names = controller_sources[joint_source].find("c:Name_array", NS).text.split()
    vertex_weights = controller.find("c:vertex_weights", NS)
    weight_inputs = [
        (item.attrib["semantic"], int(item.attrib["offset"]), item.attrib["source"].lstrip("#"))
        for item in vertex_weights.findall("c:input", NS)
    ]
    weight_stride = max(offset for _, offset, _ in weight_inputs) + 1
    joint_offset = next(offset for semantic, offset, _ in weight_inputs if semantic == "JOINT")
    weight_offset = next(offset for semantic, offset, _ in weight_inputs if semantic == "WEIGHT")
    weight_source = next(source for semantic, _, source in weight_inputs if semantic == "WEIGHT")
    weight_values = floats(controller_sources[weight_source])
    weight_counts = [int(value) for value in vertex_weights.find("c:vcount", NS).text.split()]
    weight_packed = [int(value) for value in vertex_weights.find("c:v", NS).text.split()]
    influences = []
    weight_cursor = 0
    for count in weight_counts:
        current = []
        for _ in range(count):
            record = weight_packed[weight_cursor:weight_cursor + weight_stride]
            weight_cursor += weight_stride
            current.append((record[joint_offset], weight_values[record[weight_offset]]))
        influences.append(current)

    sources = {source.attrib["id"]: tuples(source) for source in mesh.findall("c:source", NS)}
    vertices = mesh.find("c:vertices", NS)
    vertex_sources = {
        item.attrib["semantic"]: item.attrib["source"].lstrip("#")
        for item in vertices.findall("c:input", NS)
    }
    triangles = primitive_triangles(mesh, sources, vertex_sources)
    if not triangles:
        raise RuntimeError("glove triangles were not found")

    all_positions = [corner["POSITION"] for triangle in triangles for corner in triangle]
    position_values = sources[vertex_sources["POSITION"]]
    sides = {}
    scale = 0.15
    nodes = {node.attrib["id"]: node for node in root.findall(".//c:node[@type='JOINT']", NS)}
    parents = {child: parent for parent in root.iter() for child in parent}

    def global_matrix(name):
        chain = []
        node = nodes[name]
        while node is not None and node.attrib.get("type") == "JOINT":
            chain.append(node_matrix(node))
            node = parents.get(node)
        result = [[1.0 if row == column else 0.0 for column in range(4)] for row in range(4)]
        for matrix in reversed(chain):
            result = matrix_multiply(result, matrix)
        return result

    for name, sign, prefix in (("left", 1.0, "jnt_left_"), ("right", -1.0, "jnt_right_")):
        side_positions = [position for position in all_positions if position[0] * sign > 0.0]
        # Keep the original wrist origin so the already-confirmed controller and
        # tool calibration does not move. The glove cuff and exposed forearm share
        # the forearmroll bone, so the texture atlas (rather than the bone or a
        # broad position cut) is the authoritative way to remove only skin.
        forearm_roll = prefix + "forearmroll"
        forearm_roll_positions = [
            position_values[index] for index, original in enumerate(influences)
            if original and joint_names[max(original, key=lambda item: item[1])[0]] == forearm_roll
        ]
        if not forearm_roll_positions:
            raise RuntimeError(f"{forearm_roll} vertices were not found")
        wrist = min(abs(position[0]) for position in forearm_roll_positions)
        center_y = (min(position[1] for position in side_positions) + max(position[1] for position in side_positions)) * 0.5
        center_z = (min(position[2] for position in side_positions) + max(position[2] for position in side_positions)) * 0.5
        side_joint_indices = [index for index, joint in enumerate(joint_names) if joint.startswith(prefix)]
        side_index = {joint: index for index, joint in enumerate(side_joint_indices)}
        emitted = []
        removed_skin_triangles = 0
        for triangle in triangles:
            if any(corner["POSITION"][0] * sign < wrist - 1.0e-5 for corner in triangle):
                continue
            if sum(is_exposed_skin(diffuse_texture, corner["TEXCOORD"]) for corner in triangle) >= 2:
                removed_skin_triangles += 1
                continue
            for corner in triangle:
                x, y, z = corner["POSITION"][:3]
                nx, ny, nz = corner["NORMAL"][:3]
                u, v = corner["TEXCOORD"][:2]
                selected = sorted(
                    ((side_index[joint], weight) for joint, weight in influences[corner["POSITION_INDEX"]]
                     if joint in side_index),
                    key=lambda item: item[1], reverse=True)[:4]
                total = sum(weight for _, weight in selected)
                if total <= 0.0:
                    raise RuntimeError(f"{name} glove vertex has no {prefix} skin influence")
                selected += [(0, 0.0)] * (4 - len(selected))
                byte_weights = [round(weight / total * 255.0) for _, weight in selected]
                byte_weights[0] += 255 - sum(byte_weights)
                packed_indices = sum((joint & 0xff) << (slot * 8) for slot, (joint, _) in enumerate(selected))
                packed_weights = sum((weight & 0xff) << (slot * 8) for slot, weight in enumerate(byte_weights))
                emitted.append((
                    sign * (z - center_z) * scale,
                    (y - center_y) * scale,
                    -(sign * x - wrist) * scale,
                    sign * nz, ny, -sign * nx,
                    u, 1.0 - v, packed_indices, packed_weights,
                ))
        bone_pivots = []
        side_joint_names = [joint for joint in joint_names if joint.startswith(prefix)]
        finger_tokens = (("thumb", 0), ("index", 1), ("middle", 2), ("ring", 3), ("pinky", 4))
        for joint in side_joint_names:
            matrix = global_matrix(joint)
            x, y, z = matrix[0][3], matrix[1][3], matrix[2][3]
            finger, segment = -1, 0
            for token, finger_index in finger_tokens:
                marker = "hand" + token
                if marker in joint:
                    finger = finger_index
                    suffix = joint.split(marker, 1)[1]
                    segment = int(suffix[0]) if suffix and suffix[0].isdigit() else 0
                    break
            bone_pivots.append((
                sign * (z - center_z) * scale,
                (y - center_y) * scale,
                -(sign * x - wrist) * scale,
                finger, segment,
            ))
        sides[name] = (emitted, bone_pivots)
        print(f"{name}: removed {removed_skin_triangles} exposed-skin triangles; retained glove cuff")

    lines = [
        "#pragma once",
        "// Generated from the installed Scrap Mechanic 1.0 ship-mechanic glove DAE.",
        "#include <cstdint>",
        "namespace scrapvr::mechanic_hands_asset {",
        "struct Vertex { float px, py, pz, nx, ny, nz, u, v; std::uint32_t bone_indices, bone_weights; };",
        "struct BonePivot { float x, y, z; int finger, segment; };",
    ]
    for name in ("left", "right"):
        values, bone_pivots = sides[name]
        lines.append(f"inline constexpr Vertex {name}_vertices[] = {{")
        lines.extend(
            "    { " + ", ".join(cpp_float(component) for component in vertex[:8]) +
            f", 0x{vertex[8]:08x}u, 0x{vertex[9]:08x}u" + " },"
            for vertex in values
        )
        lines.append("};")
        lines.append(f"inline constexpr unsigned int {name}_vertex_count = {len(values)}u;")
        lines.append(f"inline constexpr BonePivot {name}_bone_pivots[] = {{")
        lines.extend(
            "    { " + ", ".join(cpp_float(component) for component in pivot[:3]) +
            f", {pivot[3]}, {pivot[4]}" + " },"
            for pivot in bone_pivots
        )
        lines.append("};")
        lines.append(f"inline constexpr unsigned int {name}_bone_count = {len(bone_pivots)}u;")
    lines.append("}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"generated {len(sides['left'][0])} left and {len(sides['right'][0])} right vertices, "
          f"{len(sides['left'][1])} bones per hand")


if __name__ == "__main__":
    main()
