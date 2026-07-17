#!/usr/bin/env python3
"""
generate_rocket_glb.py

Generates a simple, recognizable amateur-rocket mesh as a self-contained binary glTF
(.glb) with NO third-party dependencies (stdlib only).

Geometry (meters, matching the JSBSim 'rocket' model proportions: 6" dia x 109" long):
  - a body tube,
  - a conical nose,
  - four fins at the tail,
  - a short nozzle stub.

Orientation convention (IMPORTANT): the nose points along +X and the tail sits at X=0,
so it matches ARocketPawn / the JSBSimFlightDynamicsModel structural frame, where the
actor's +X axis is the rocket's forward/nose direction. +Z is "up" in the model.

Usage:
  python Tools/generate_rocket_glb.py [output_path]
Default output: RawAssets/rocket.glb
"""

import math
import os
import sys
import json
import struct

# ---- Rocket dimensions in meters (6" diameter, 109" length) --------------------------
R = 0.0762            # body radius (3 inch)
LEN_TOTAL = 2.769     # 109 inch
NOSE_LEN = 0.55       # conical nose length
NOZZLE_LEN = 0.12
NOZZLE_R = 0.045
FIN_ROOT = 0.45       # fin root chord (along X, at the tail)
FIN_TIP = 0.18        # fin tip chord
FIN_SPAN = 0.16       # how far fins extend past the body radius
FIN_THICK = 0.010
SEG = 28              # radial segments

# X positions (tail at 0, nose tip at LEN_TOTAL)
X_TAIL = 0.0
X_BODY_TOP = LEN_TOTAL - NOSE_LEN
X_NOSE_TIP = LEN_TOTAL

positions = []   # list of (x, y, z)
indices = []     # flat list of ints


def add_vertex(p):
    positions.append(p)
    return len(positions) - 1


def add_tri(a, b, c):
    indices.extend((a, b, c))


def add_quad(a, b, c, d):
    add_tri(a, b, c)
    add_tri(a, c, d)


def ring(x, radius):
    """Create a ring of SEG vertices at plane x, return their indices."""
    idx = []
    for i in range(SEG):
        ang = 2.0 * math.pi * i / SEG
        y = radius * math.cos(ang)
        z = radius * math.sin(ang)
        idx.append(add_vertex((x, y, z)))
    return idx


def bridge_rings(r0, r1, flip=False):
    """Create quads between two equal-length rings."""
    n = len(r0)
    for i in range(n):
        a = r0[i]
        b = r0[(i + 1) % n]
        c = r1[(i + 1) % n]
        d = r1[i]
        if flip:
            add_quad(a, d, c, b)
        else:
            add_quad(a, b, c, d)


# ---- Body tube -----------------------------------------------------------------------
ring_tail = ring(X_TAIL, R)
ring_shoulder = ring(X_BODY_TOP, R)
bridge_rings(ring_tail, ring_shoulder)

# tail cap (disk) so the bottom is closed
center_tail = add_vertex((X_TAIL, 0.0, 0.0))
for i in range(SEG):
    add_tri(center_tail, ring_tail[(i + 1) % SEG], ring_tail[i])

# ---- Nose cone -----------------------------------------------------------------------
nose_tip = add_vertex((X_NOSE_TIP, 0.0, 0.0))
for i in range(SEG):
    a = ring_shoulder[i]
    b = ring_shoulder[(i + 1) % SEG]
    add_tri(a, b, nose_tip)

# ---- Nozzle stub (a small flared cone below the tail, pointing -X) --------------------
ring_nozzle_top = ring(X_TAIL, NOZZLE_R * 0.6)
ring_nozzle_exit = ring(X_TAIL - NOZZLE_LEN, NOZZLE_R)
bridge_rings(ring_nozzle_top, ring_nozzle_exit, flip=True)

# ---- Fins (4, every 90 deg around the tail) ------------------------------------------
def add_fin(angle_deg):
    ca = math.cos(math.radians(angle_deg))
    sa = math.sin(math.radians(angle_deg))
    # local fin plane: radial direction (out) and thickness direction (tangent)
    out = (0.0, ca, sa)
    tan = (0.0, -sa, ca)

    def P(x, radial, thick):
        return (
            x,
            out[1] * radial + tan[1] * thick,
            out[2] * radial + tan[2] * thick,
        )

    x_root_le = FIN_ROOT          # leading edge at root (forward)
    x_root_te = 0.0               # trailing edge at root (tail)
    x_tip_le = FIN_TIP            # leading edge at tip
    x_tip_te = 0.0
    r_in = R * 0.85
    r_out = R + FIN_SPAN

    for t in (FIN_THICK * 0.5, -FIN_THICK * 0.5):
        a = add_vertex(P(x_root_te, r_in, t))
        b = add_vertex(P(x_root_le, r_in, t))
        c = add_vertex(P(x_tip_le, r_out, t))
        d = add_vertex(P(x_tip_te, r_out, t))
        if t > 0:
            add_quad(a, b, c, d)
        else:
            add_quad(a, d, c, b)


for a in (0, 90, 180, 270):
    add_fin(a)


# ---- Pack into GLB -------------------------------------------------------------------
def build_glb():
    # Interleave nothing; two accessors (positions float32, indices uint32).
    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    idx_bytes = b"".join(struct.pack("<I", i) for i in indices)

    # align each bufferView to 4 bytes
    def pad4(b):
        while len(b) % 4 != 0:
            b += b"\x00"
        return b

    pos_bytes = pad4(pos_bytes)
    idx_off = len(pos_bytes)
    idx_bytes = pad4(idx_bytes)
    bin_blob = pos_bytes + idx_bytes

    # bounds for POSITION accessor
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    pmin = [min(xs), min(ys), min(zs)]
    pmax = [max(xs), max(ys), max(zs)]

    gltf = {
        "asset": {"version": "2.0", "generator": "generate_rocket_glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Rocket"}],
        "meshes": [{
            "name": "Rocket",
            "primitives": [{
                "attributes": {"POSITION": 0},
                "indices": 1,
                "material": 0,
                "mode": 4,
            }],
        }],
        "materials": [{
            "name": "RocketBody",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.86, 0.9, 1.0],
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
            {"bufferView": 0, "componentType": 5126, "count": len(positions),
             "type": "VEC3", "min": pmin, "max": pmax},
            {"bufferView": 1, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }

    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(json_bytes) % 4 != 0:
        json_bytes += b" "
    while len(bin_blob) % 4 != 0:
        bin_blob += b"\x00"

    total = 12 + 8 + len(json_bytes) + 8 + len(bin_blob)
    out = bytearray()
    out += struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<I4s", len(json_bytes), b"JSON")
    out += json_bytes
    out += struct.pack("<I4s", len(bin_blob), b"BIN\x00")
    out += bin_blob
    return bytes(out)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "RawAssets", "rocket.glb")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    glb = build_glb()
    with open(out_path, "wb") as f:
        f.write(glb)
    print(f"Wrote {out_path}")
    print(f"  vertices: {len(positions)}   triangles: {len(indices)//3}   bytes: {len(glb)}")


if __name__ == "__main__":
    main()
