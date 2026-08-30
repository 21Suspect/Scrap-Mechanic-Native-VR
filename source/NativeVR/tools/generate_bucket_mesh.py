#!/usr/bin/env python3
"""Emit a compact held-bucket mesh for the native VR tool pass."""

from math import cos, pi, sin
from pathlib import Path


def cpp_float(value):
    text = f"{value:.7g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def emit(path, name, vertices):
    lines = [
        f"inline constexpr Vertex {name}_vertices[] = {{",
    ]
    for px, py, pz, nx, ny, nz, u, v in vertices:
        lines.append(
            "    { "
            + ", ".join(cpp_float(value) for value in (px, py, pz, nx, ny, nz, u, v))
            + " },"
        )
    lines.append("};")
    lines.append(f"inline constexpr unsigned int {name}_vertex_count = {len(vertices)}u;")
    path.write_text("\n".join(lines) + "\n")


def tri(vertices, a, b, c):
    vertices.extend((a, b, c))


def ring(radius, y, segments):
    return [
        (radius * cos(2 * pi * i / segments), y, radius * sin(2 * pi * i / segments))
        for i in range(segments)
    ]


def add_quad(vertices, p00, p10, p11, p01, nu, nv):
    def vertex(p, u, v):
        return (p[0], p[1], p[2], nu, nv, 0.0 if abs(nu) + abs(nv) > 0 else 1.0, u, v)

    # Recompute a proper face normal from the quad.
    ax, ay, az = p10[0] - p00[0], p10[1] - p00[1], p10[2] - p00[2]
    bx, by, bz = p01[0] - p00[0], p01[1] - p00[1], p01[2] - p00[2]
    nx, ny, nz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
    length = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
    nx, ny, nz = nx / length, ny / length, nz / length

    def v(p, u, v_coord):
        return (p[0], p[1], p[2], nx, ny, nz, u, v_coord)

    tri(vertices, v(p00, 0.0, 0.0), v(p10, 1.0, 0.0), v(p11, 1.0, 1.0))
    tri(vertices, v(p00, 0.0, 0.0), v(p11, 1.0, 1.0), v(p01, 0.0, 1.0))


def cylinder_shell(bottom_r, top_r, y0, y1, segments, invert=False):
    vertices = []
    bottom = ring(bottom_r, y0, segments)
    top = ring(top_r, y1, segments)
    for i in range(segments):
        j = (i + 1) % segments
        if invert:
            add_quad(vertices, top[i], top[j], bottom[j], bottom[i], 0.0, 0.0)
        else:
            add_quad(vertices, bottom[i], bottom[j], top[j], top[i], 0.0, 0.0)
    return vertices


def disc(radius, y, segments, normal_y):
    vertices = []
    center = (0.0, y, 0.0, 0.0, normal_y, 0.0, 0.5, 0.5)
    points = ring(radius, y, segments)
    for i in range(segments):
        j = (i + 1) % segments
        a = (points[i][0], y, points[i][2], 0.0, normal_y, 0.0, 0.5 + 0.5 * points[i][0] / radius, 0.5 + 0.5 * points[i][2] / radius)
        b = (points[j][0], y, points[j][2], 0.0, normal_y, 0.0, 0.5 + 0.5 * points[j][0] / radius, 0.5 + 0.5 * points[j][2] / radius)
        if normal_y >= 0:
            tri(vertices, center, a, b)
        else:
            tri(vertices, center, b, a)
    return vertices


def torus_arc(radius, tube, y, segments, tube_segments, start, sweep):
    vertices = []
    for i in range(segments):
        t0 = start + sweep * i / segments
        t1 = start + sweep * (i + 1) / segments
        for k in range(tube_segments):
            s0 = 2 * pi * k / tube_segments
            s1 = 2 * pi * (k + 1) / tube_segments

            def point(theta, phi):
                cx = radius * cos(theta)
                cz = radius * sin(theta)
                px = cx + tube * cos(phi) * cos(theta)
                py = y + tube * sin(phi)
                pz = cz + tube * cos(phi) * sin(theta)
                nx = cos(phi) * cos(theta)
                ny = sin(phi)
                nz = cos(phi) * sin(theta)
                return (px, py, pz, nx, ny, nz, theta / (2 * pi), phi / (2 * pi))

            p00 = point(t0, s0)
            p10 = point(t1, s0)
            p11 = point(t1, s1)
            p01 = point(t0, s1)
            tri(vertices, p00, p10, p11)
            tri(vertices, p00, p11, p01)
    return vertices


def main():
    segments = 20
    body = []
    body += cylinder_shell(0.22, 0.30, 0.00, 0.72, segments)
    body += cylinder_shell(0.20, 0.28, 0.06, 0.70, segments, invert=True)
    body += disc(0.22, 0.00, segments, -1.0)
    body += disc(0.20, 0.06, segments, 1.0)
    # Rim
    body += cylinder_shell(0.30, 0.32, 0.70, 0.76, segments)
    body += cylinder_shell(0.28, 0.30, 0.70, 0.76, segments, invert=True)
    body += disc(0.32, 0.76, segments, 1.0)
    body += disc(0.30, 0.70, segments, -1.0)

    handle = torus_arc(0.26, 0.035, 0.92, 14, 8, pi * 0.12, pi * 0.76)

    liquid = disc(0.255, 0.48, segments, 1.0)
    # Give the liquid a little thickness so it reads in stereo.
    liquid += disc(0.255, 0.42, segments, -1.0)
    liquid += cylinder_shell(0.255, 0.255, 0.42, 0.48, segments)

    out = Path(__file__).resolve().parents[1] / "src" / "bucket_tool_asset.hpp"
    chunks = [
        "#pragma once",
        "// Procedural held bucket used when VR hides the first-person viewmodel.",
        "namespace scrapvr::bucket_tool_asset {",
        "using Vertex = native_tool_asset::Vertex;",
    ]
    tmp = Path("/tmp")
    emit(tmp / "bucket_body.inc", "bucket_body", body)
    emit(tmp / "bucket_handle.inc", "bucket_handle", handle)
    emit(tmp / "bucket_liquid.inc", "bucket_liquid", liquid)
    chunks.append((tmp / "bucket_body.inc").read_text().rstrip())
    chunks.append((tmp / "bucket_handle.inc").read_text().rstrip())
    chunks.append((tmp / "bucket_liquid.inc").read_text().rstrip())
    chunks.append("}")
    out.write_text("\n".join(chunks) + "\n")
    print(f"wrote {out} body={len(body)} handle={len(handle)} liquid={len(liquid)}")


if __name__ == "__main__":
    main()
