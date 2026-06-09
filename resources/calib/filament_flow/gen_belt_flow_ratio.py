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

HERE = os.path.dirname(os.path.abspath(__file__))

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
    m = build()
    bb = np.round(m.bounds, 2)
    s = m.vertices[:, 1] + m.vertices[:, 2]
    mo, nbad = overhang_report(m)
    print(f"PITCH={PITCH:.4f}  T_READ={T_READ:.3f}  READ_LAYERS={READ_LAYERS}")
    print(f"bbox min={bb[0]}  max={bb[1]}  extents={np.round(m.extents,2)}")
    print(f"print_z (Y+Z) span: {s.min():.3f} .. {s.max():.3f}  ({(s.max()-s.min())/PITCH:.0f} layers)")
    print(f"read window: top {READ_LAYERS} layers, face ~{WB*np.sqrt(2):.1f}mm x {PAD_X:.0f}mm")
    print(f"watertight={m.is_watertight}  overhang_max={mo:.1f}deg  bad_faces={nbad}")
    m.export(OUT)
    print(f"-> {OUT}")
