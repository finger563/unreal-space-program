#!/usr/bin/env python3
"""
generate_rocket_glb.py

Generates the amateur-rocket meshes as self-contained binary glTF (.glb) files with
NO third-party dependencies (stdlib only).

Outputs (in RawAssets/):
  rocket.glb          - the full assembled rocket (single mesh)
  rocket_booster.glb  - booster section: tube + 4 fins + nozzle stub
  rocket_upper.glb    - upper airframe tube
  rocket_nose.glb     - nose cone
  chute_canopy.glb    - parachute canopy dome (unit ~1 m diameter; scale in-engine)

Conventions (matching ARocketPawn / the JSBSim structural frame mapping):
  - +X is "toward the nose" (up, when the rocket stands vertically). +Z is model "up".
  - The FULL rocket spans X [0 .. 2.769 m]: TAIL at the origin, nose tip at +2.769.
  - Each SECTION mesh has its origin at its AFT joint and spans X [0 .. section length],
    so it can be attached directly to the pawn's section Root components (Booster at
    actor X=0, Upper at 110 cm, Nose at 215 cm) with identity rotation and scale 1.
  - The canopy's origin is at the center of its rim (suspension-line attachment plane),
    dome apex toward +X; it is double-sided so the inside is visible.

Usage:
  python Tools/generate_rocket_glb.py [output_dir]
"""

import math
import os
import sys
import json
import struct

# ---- Rocket dimensions in meters (6" diameter, 109" length) --------------------------
R = 0.0762             # body radius (3 inch)
LEN_TOTAL = 2.769      # 109 inch
BOOSTER_LEN = 1.10     # booster section (matches ARocketPawn's BoosterRoot span)
UPPER_LEN = 1.05       # upper airframe section
NOSE_LEN = LEN_TOTAL - BOOSTER_LEN - UPPER_LEN  # 0.619 - nose cone
NOZZLE_LEN = 0.12
NOZZLE_R = 0.045
FIN_ROOT = 0.45        # fin root chord (along X, at the tail)
FIN_TIP = 0.18         # fin tip chord
FIN_SPAN = 0.16        # how far fins extend past the body radius
FIN_THICK = 0.010
SEG = 28               # radial segments

CANOPY_R = 0.5         # canopy dome radius (unit-ish; scaled in-engine)
CANOPY_RINGS = 7


class Mesh:
    """Minimal triangle-mesh accumulator with glb export."""

    def __init__(self):
        self.positions = []
        self.indices = []

    def add_vertex(self, p):
        self.positions.append(p)
        return len(self.positions) - 1

    def add_tri(self, a, b, c):
        self.indices.extend((a, b, c))

    def add_quad(self, a, b, c, d):
        self.add_tri(a, b, c)
        self.add_tri(a, c, d)

    # ---- shared primitives ----

    def ring(self, x, radius):
        idx = []
        for i in range(SEG):
            ang = 2.0 * math.pi * i / SEG
            idx.append(self.add_vertex((x, radius * math.cos(ang), radius * math.sin(ang))))
        return idx

    def bridge_rings(self, r0, r1, flip=False):
        n = len(r0)
        for i in range(n):
            a, b = r0[i], r0[(i + 1) % n]
            c, d = r1[(i + 1) % n], r1[i]
            if flip:
                self.add_quad(a, d, c, b)
            else:
                self.add_quad(a, b, c, d)

    def cap(self, ring_idx, x, flip=False):
        center = self.add_vertex((x, 0.0, 0.0))
        n = len(ring_idx)
        for i in range(n):
            a, b = ring_idx[i], ring_idx[(i + 1) % n]
            if flip:
                self.add_tri(center, a, b)
            else:
                self.add_tri(center, b, a)
        return center

    def tube(self, x0, x1, radius, cap0=True, cap1=True):
        r0 = self.ring(x0, radius)
        r1 = self.ring(x1, radius)
        self.bridge_rings(r0, r1)
        if cap0:
            self.cap(r0, x0)
        if cap1:
            self.cap(r1, x1, flip=True)
        return r0, r1

    def cone(self, x_base, x_tip, radius, cap_base=True):
        rb = self.ring(x_base, radius)
        tip = self.add_vertex((x_tip, 0.0, 0.0))
        for i in range(SEG):
            self.add_tri(rb[i], rb[(i + 1) % SEG], tip)
        if cap_base:
            self.cap(rb, x_base)

    def nozzle(self, x_tail):
        r_top = self.ring(x_tail, NOZZLE_R * 0.6)
        r_exit = self.ring(x_tail - NOZZLE_LEN, NOZZLE_R)
        self.bridge_rings(r_top, r_exit, flip=True)

    def fin(self, angle_deg):
        ca, sa = math.cos(math.radians(angle_deg)), math.sin(math.radians(angle_deg))
        out, tan = (0.0, ca, sa), (0.0, -sa, ca)

        def P(x, radial, thick):
            return (x, out[1] * radial + tan[1] * thick, out[2] * radial + tan[2] * thick)

        r_in, r_out = R * 0.85, R + FIN_SPAN
        for t in (FIN_THICK * 0.5, -FIN_THICK * 0.5):
            a = self.add_vertex(P(0.0, r_in, t))
            b = self.add_vertex(P(FIN_ROOT, r_in, t))
            c = self.add_vertex(P(FIN_TIP, r_out, t))
            d = self.add_vertex(P(0.0, r_out, t))
            if t > 0:
                self.add_quad(a, b, c, d)
            else:
                self.add_quad(a, d, c, b)

    def canopy(self, radius):
        """Hemispherical dome, rim in the YZ plane at x=0, apex at +x=radius.
        Rendered double-sided via the material, so a single surface suffices."""
        rings = []
        for j in range(CANOPY_RINGS):
            phi = (math.pi / 2.0) * j / CANOPY_RINGS  # 0 = rim .. pi/2 = apex
            rings.append(self.ring(radius * math.sin(phi), radius * math.cos(phi)))
        apex = self.add_vertex((radius, 0.0, 0.0))
        for j in range(CANOPY_RINGS - 1):
            self.bridge_rings(rings[j], rings[j + 1])
        for i in range(SEG):
            self.add_tri(rings[-1][i], rings[-1][(i + 1) % SEG], apex)

    # ---- export ----

    def to_glb(self, name, color, double_sided=False):
        pos_bytes = b"".join(struct.pack("<3f", *p) for p in self.positions)
        idx_bytes = b"".join(struct.pack("<I", i) for i in self.indices)

        def pad4(b, char=b"\x00"):
            while len(b) % 4 != 0:
                b += char
            return b

        pos_bytes = pad4(pos_bytes)
        idx_off = len(pos_bytes)
        idx_bytes = pad4(idx_bytes)
        bin_blob = pos_bytes + idx_bytes

        xs = [p[0] for p in self.positions]
        ys = [p[1] for p in self.positions]
        zs = [p[2] for p in self.positions]

        gltf = {
            "asset": {"version": "2.0", "generator": "generate_rocket_glb.py"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0, "name": name}],
            "meshes": [{
                "name": name,
                "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0, "mode": 4}],
            }],
            "materials": [{
                "name": name + "Material",
                "doubleSided": double_sided,
                "pbrMetallicRoughness": {
                    "baseColorFactor": color,
                    "metallicFactor": 0.1,
                    "roughnessFactor": 0.55,
                },
            }],
            "buffers": [{"byteLength": len(bin_blob)}],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_bytes), "target": 34962},
                {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": len(self.positions),
                 "type": "VEC3", "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
                {"bufferView": 1, "componentType": 5125, "count": len(self.indices), "type": "SCALAR"},
            ],
        }

        json_bytes = pad4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"), b" ")
        total = 12 + 8 + len(json_bytes) + 8 + len(bin_blob)
        out = bytearray()
        out += struct.pack("<4sII", b"glTF", 2, total)
        out += struct.pack("<I4s", len(json_bytes), b"JSON")
        out += json_bytes
        out += struct.pack("<I4s", len(bin_blob), b"BIN\x00")
        out += bin_blob
        return bytes(out)


BODY_COLOR = [0.85, 0.86, 0.90, 1.0]
FIN_COLOR = BODY_COLOR
NOSE_COLOR = [0.80, 0.20, 0.15, 1.0]
CANOPY_COLOR = [0.95, 0.45, 0.10, 1.0]


def build_full():
    m = Mesh()
    m.tube(0.0, LEN_TOTAL - NOSE_LEN, R, cap0=True, cap1=False)
    m.cone(LEN_TOTAL - NOSE_LEN, LEN_TOTAL, R, cap_base=False)
    m.nozzle(0.0)
    for a in (0, 90, 180, 270):
        m.fin(a)
    return m


def build_booster():
    m = Mesh()
    m.tube(0.0, BOOSTER_LEN, R)
    m.nozzle(0.0)
    for a in (0, 90, 180, 270):
        m.fin(a)
    return m


def build_upper():
    m = Mesh()
    m.tube(0.0, UPPER_LEN, R)
    return m


def build_nose():
    m = Mesh()
    m.cone(0.0, NOSE_LEN, R, cap_base=True)
    return m


def build_canopy():
    m = Mesh()
    m.canopy(CANOPY_R)
    return m


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "RawAssets")
    os.makedirs(out_dir, exist_ok=True)

    outputs = [
        ("rocket.glb", build_full(), BODY_COLOR, False),
        ("rocket_booster.glb", build_booster(), FIN_COLOR, False),
        ("rocket_upper.glb", build_upper(), BODY_COLOR, False),
        ("rocket_nose.glb", build_nose(), NOSE_COLOR, False),
        ("chute_canopy.glb", build_canopy(), CANOPY_COLOR, True),
    ]

    for filename, mesh, color, double_sided in outputs:
        path = os.path.join(out_dir, filename)
        glb = mesh.to_glb(os.path.splitext(filename)[0], color, double_sided)
        with open(path, "wb") as f:
            f.write(glb)
        print(f"Wrote {path}  ({len(mesh.positions)} verts, {len(mesh.indices)//3} tris, {len(glb)} bytes)")


if __name__ == "__main__":
    main()
