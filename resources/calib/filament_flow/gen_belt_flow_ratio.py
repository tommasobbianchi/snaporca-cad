#!/usr/bin/env python3
"""Belt flow-ratio calibration asset generator (orca-pr12998, IR3V2 belt).

The cartesian Orca flow-ratio test reads a flat TOP SOLID INFILL surface: each
pad is printed at a different print_flow_ratio and you pick the pad whose top
face has neither gaps (under-extrusion) nor ridges (over-extrusion). A flat pad
lying on a 45 deg belt cannot reproduce that read: the belt slices constant
print_z = (Y_model + Z_model) planes, so a thin flat pad's "top" is a smeared
diagonal sliver, not a flat readable area.

Belt-native fix (user spec): tilt the reading pad 45 deg about model-X so its
reading face normal becomes the belt-normal (0,1,1)/sqrt2. That face is then a
plane of CONSTANT print_z -> it prints as ONE flat top layer (and the ~N layers
below it become the top solid infill = the read window), laid down LAST. Under
the pad sits a triangular WEDGE rooted on the belt (keel-first, print_z=0 tip);
the wedge body is sliced as sparse infill (fast, little filament), only the pad
is solid. The original pad's shallow registration tab is dropped; the pad's
down-belt edge is carried by the wedge's own end wall.

Cross-section is built in the model Y-Z plane and extruded along model-X:

    model-Z (gantry) ^
                     |   T_out
              read   |  /  \\  reading face  (normal = belt-normal, const print_z)
              face ->| /    \\ B_out
                     |/  pad  \\
                  T  +--------+ . . . hypotenuse T->B  (const print_z, body top)
                     |\\ wedge |
       leading wall  | \\ body |
       (Y=0)         |  \\     |
                     +---+-----+----> model-Y (belt advance)
                     K        B
                  (0,0)     (Wb,0)         K..B = belt contact (Z=0)

print_z = Y+Z. K=(0,0) is the keel tip (print_z 0, first to enter the belt).
T->B (hypotenuse) and B_out->T_out (reading face) are both constant-print_z
lines (slope -1), so the top of the slab is a true flat top surface.

Geometry contract shared with the C++ calib_flowrate belt branch (Plater.cpp):
  READ_LAYERS below sizes the slab thickness; the C++ sets top_shell_layers to
  match so the whole slab is solid (the read window) and the wedge body stays
  sparse. Keep them in sync.
"""
import numpy as np, trimesh, os
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from shapely.geometry import Polygon as ShPoly
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))

# Flow modifiers used by the cartesian passes (flowrate-test-pass1/2.3mf): one labeled
# pad STL per value. C++ loads belt_flow_ratio_<suffix>.stl (suffix: m20..m1,0,5..20).
MODS_ALL = [-20, -15, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 5, 10, 15, 20]
TEXT_H        = 5.0    # engraved digit height (mm)
TEXT_DEPTH    = 0.8    # cut INTO the lateral face (engraved, not embossed -> belt-safe)
TEXT_OVERSHOOT= 0.6    # poke out of the face for a clean boolean cut
TEXT_CY, TEXT_CZ = 7.0, 6.0   # engraving centre on the wedge body (Y,Z), below the read slab

# --- parameters -------------------------------------------------------------
PAD_X       = 30.0     # lateral (model-X) width of the reading area
WB          = 24.0     # wedge base along model-Y on the belt == hypotenuse print_z
LAYER_H     = 0.2      # belt-normal layer height (process default)
PITCH       = LAYER_H / np.cos(np.pi / 4.0)   # print_z advance per belt layer (~0.283)
READ_LAYERS = 12       # solid top layers = the read window (C++ top_shell_layers match)
T_READ      = READ_LAYERS * PITCH / np.sqrt(2.0)   # slab normal thickness for that span

OUT = os.path.join(HERE, "belt_flow_ratio.stl")


def build():
    off = T_READ / np.sqrt(2.0)                 # YZ offset of the slab along (1,1)/sqrt2
    # cross-section outline in (Y, Z), CCW, simple pentagon (wedge + reading slab)
    K     = (0.0,            0.0)                # keel tip, print_z 0
    B     = (WB,             0.0)               # trailing belt corner, print_z WB
    B_out = (WB + off,       off)               # down-belt end of reading face
    T_out = (off,            WB + off)          # up-belt end of reading face
    T     = (0.0,            WB)                # top of leading wall, print_z WB
    poly2d = [K, B, B_out, T_out, T]

    # extrude the YZ section along +Z_poly, then permute axes so the extrusion
    # axis becomes model-X: (px,py,pz) -> (X,Y,Z) = (pz, px, py)  (det +1, no mirror)
    from shapely.geometry import Polygon as ShPoly
    prism = trimesh.creation.extrude_polygon(ShPoly(poly2d), height=PAD_X)
    P = np.array([[0, 0, 1.0],
                  [1, 0, 0.0],
                  [0, 1, 0.0]])
    M = np.eye(4); M[:3, :3] = P
    prism.apply_transform(M)

    # keel-first: min(Y+Z) == 0 and rest on belt (min Z == 0); centre X at 0
    v = prism.vertices
    s = v[:, 1] + v[:, 2]
    prism.apply_translation([-(v[:, 0].min() + v[:, 0].max()) / 2.0, 0.0, -v[:, 2].min()])
    return prism


def text_mesh(s):
    """2D text -> solid extruded along local +Z, centred in X/Y (from gen_belt_temp_tower)."""
    tp = TextPath((0, 0), s, size=TEXT_H, prop=FontProperties(family='DejaVu Sans'))
    rings = [ShPoly(p) for p in tp.to_polygons() if len(p) >= 3]
    rings.sort(key=lambda r: r.area, reverse=True)
    used = [False] * len(rings); parts = []
    for i, o in enumerate(rings):
        if used[i]: continue
        holes = []
        for j in range(i + 1, len(rings)):
            if not used[j] and o.contains(rings[j]): holes.append(rings[j].exterior.coords); used[j] = True
        parts.append(ShPoly(o.exterior.coords, holes)); used[i] = True
    poly = unary_union(parts)
    geoms = list(poly.geoms) if poly.geom_type == 'MultiPolygon' else [poly]
    m = trimesh.util.concatenate([trimesh.creation.extrude_polygon(g, height=TEXT_DEPTH + TEXT_OVERSHOOT) for g in geoms])
    c = m.bounds.mean(axis=0); m.apply_translation([-c[0], -c[1], 0]); return m


def engrave(prism, label):
    """CUT `label` into the +X lateral face (a vertical print-space wall -> belt-safe; the
    top read face is untouched). Basis maps text rightward to -Y so it reads upright when
    viewed from +X; extrudes along +X, straddling the face for a clean boolean difference."""
    x_face = prism.vertices[:, 0].max()
    t = text_mesh(label)
    # local (lx,ly,lz) -> model: x=lz (depth), y=-lx (text width, reads correct from +X), z=ly
    R = np.array([[0.0, 0.0, 1.0],
                  [-1.0, 0.0, 0.0],
                  [0.0, 1.0, 0.0]])
    M = np.eye(4); M[:3, :3] = R; t.apply_transform(M)
    # straddle the face: model-X from (x_face - DEPTH) .. (x_face + OVERSHOOT); centre on body
    t.apply_translation([x_face - TEXT_DEPTH, TEXT_CY, TEXT_CZ])
    out = trimesh.boolean.difference([prism, t], engine='manifold')
    return out


def label_for(mod):
    return "0" if mod == 0 else f"{mod:+d}"          # "-20", "0", "+20"

def suffix_for(mod):
    return ("m%d" % -mod) if mod < 0 else str(mod)   # m20..m1, 0, 5..20  (matches C++)


def overhang_report(m):
    """Flag faces steeper than 45 deg overhang in belt gravity g=(0,-1,-1)/sqrt2,
    excluding the belt-contact bottom. Returns (max_overhang_deg, n_bad)."""
    g = np.array([0.0, -1.0, -1.0]) / np.sqrt(2.0)
    fn = m.face_normals
    # overhang angle: how far a downward-facing face leans past vertical.
    dot = fn @ (-g)                       # +1 => faces straight up (top), -1 => straight down
    facing_down = dot < 0                 # underside faces
    # bottom belt-contact faces (normal ~ -Z) are supported by the conveyor: exclude
    on_belt = (fn[:, 2] < -0.7) & (np.abs(m.triangles_center[:, 2]) < 0.5)
    overh = facing_down & ~on_belt
    if not overh.any():
        return 0.0, 0
    ang = np.degrees(np.arccos(np.clip(-dot[overh], -1, 1)))   # 0 = straight down
    return float(ang.max()), int(overh.sum())


if __name__ == "__main__":
    base = build()
    bb = np.round(base.bounds, 2)
    mo, nbad = overhang_report(base)
    print(f"PITCH={PITCH:.4f}  T_READ={T_READ:.3f}  READ_LAYERS={READ_LAYERS}")
    print(f"base bbox min={bb[0]} max={bb[1]}  read face ~{WB*np.sqrt(2):.1f}x{PAD_X:.0f}mm  "
          f"watertight={base.is_watertight} overhang_max={mo:.1f}deg")
    base.export(OUT)                       # unlabeled fallback (belt_flow_ratio.stl)
    print(f"-> {os.path.basename(OUT)}")

    # one labeled pad per flow modifier (flow % engraved on the +X lateral face)
    for mod in MODS_ALL:
        m = engrave(build(), label_for(mod))
        out = os.path.join(HERE, f"belt_flow_ratio_{suffix_for(mod)}.stl")
        m.export(out)
        s = m.vertices[:, 1] + m.vertices[:, 2]
        print(f"  {label_for(mod):>4}: watertight={m.is_watertight}  "
              f"print_z {s.min():.2f}..{s.max():.2f}  -> {os.path.basename(out)}")
