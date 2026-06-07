#!/usr/bin/env python3
"""Belt Pressure-Advance (PA) calibration asset generator (orca-pr12998, belt-7pg).

Belt printers can't run the cartesian PA "pattern a righe": the flat anchor frame and the
isotropic-axis assumptions break under the 45 deg belt transform (Z_virt = Y_model + Z_model,
Y_mach = sqrt(2)*Y_virt + shear flow). This builds the belt-native equivalent.

  * N discrete single-wall PROVINI in a row along the belt (designed-Y), one PA value each.
    Each belt layer of a wall is a single STRAIGHT trace along machine-X, stacked belt-normal.

    *** Walls MUST be straight in model-Y. ***  On a belt, model-Y drives the BELT axis
    (machine Z, gcode_remap_z=pos_y). Within one layer the only crash-safe motion is pure
    machine-X: any in-plane "up-the-slope" move changes model-Y == moves the belt. So an
    Ellis-style ZIGZAG (in-plane corners) would oscillate the belt back-and-forth several mm
    per layer = the Z-oscillation that destroys belt drivers (the #1 belt safety rule). It is
    geometrically forbidden here, not just suboptimal. The PA signal therefore comes from the
    X-end reversals + a slow/fast SPEED modulation along X injected by the gcode (phase 2,
    the classic line method), NOT from geometry corners.
  * The PA value is CUT (a recessed void) into the top of the base beside each wall - no tall
    inclined plaque (that was a pleonasm). A cut, not a raised number, so it is never an
    unsupported Y-overhang on belt (belt-bz6). Reads flat off the finished part; near-zero
    extra print time.
  * A thin connector RIBBON/base under everything -> the whole sweep peels off the belt as ONE
    monoblock, and carries the engraved numbers.

Built keel-first (min Y = min Z = 0 at the leading edge) so layer 0 always has material
(R11 lesson). Walls are READING_LAYERS tall (every layer reads; only the 1st layer after the
PA change settles). The base is continuous, so there is no empty inter-provino gap to drop a
custom-gcode event into; PITCH below is the shared geometry contract with the future C++
Plater::calib_pa branch, which injects SET_PRESSURE_ADVANCE per provino at print_z inside the
wall body. Keep them in sync.
"""
import numpy as np, trimesh, os
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from shapely.geometry import LineString, Polygon as ShPoly
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- geometry contract (keep in sync with the C++ calib_pa branch) ----
WALL_LEN   = 40.0    # X length of each wall (mm)
WALL_THK   = 0.45    # single-perimeter wall thickness (mm) ~ 0.4 nozzle
# reading height = N belt layers. Every layer reads (the along-X speed modulation puts the PA
# artifact in each layer's trace); only the 1st layer after the SET_PRESSURE_ADVANCE settles.
READING_LAYERS = 24
LAYER_H_NOM = 0.2    # nominal belt-normal layer height the asset is sized for
VPITCH = LAYER_H_NOM / np.cos(np.radians(45))   # virtual-Z pitch per layer ~0.283
WALL_H = round(READING_LAYERS * VPITCH, 3)       # model-Z height of the test wall
BASE_H     = 0.6     # connector base thickness in Z (mm) — thin raft (~2-3 belt layers)
NUM_W      = 22.0    # X span available for the number, beside the wall
NUM_GAP    = 3.0     # X gap between wall end and number region
NUM_H      = 4.0     # digit cap-height in designed-Y (mm)
NUM_WIDTH_SCALE = 1.5  # stretch digits 50% wider in X (more legible) without growing the Y pitch
CUT_OVER   = 0.6     # overshoot beyond both base faces for a clean THROUGH (full) cut
BRIDGE_W   = 0.6     # stencil-bridge width keeping each counter island (0/4/6/8/9) attached
GAP_Y      = 3.5     # designed-Y gap between provini (kept above the ~2.6mm min so the tall
                     # walls don't overlap in print_z, which would make the per-layer PA ambiguous)
EMBED      = 0.3     # how far walls dip into the base (union robustness)

PROV_Y = max(WALL_THK, NUM_H + 0.5)      # designed-Y footprint of one provino
PITCH  = PROV_Y + GAP_Y                   # designed-Y pitch  (C++ contract)
RIB_X  = WALL_LEN + NUM_GAP + NUM_W       # base X extent

# default PA sweep (parametric). True per-machine range comes from the calib dialog later.
PA_VALUES = [round(0.01 * i, 2) for i in range(13)]  # 0.00 .. 0.12 step 0.01


def wall(y0):
    """Thin STRAIGHT single-wall along machine-X, standing in Z, rooted into the ribbon.

    Straight in model-Y on purpose: any model-Y variation within a layer = belt oscillation
    (see module docstring). Each belt layer slices to one clean machine-X trace."""
    xs = np.array([0.0, WALL_LEN]); ys = np.array([0.0, 0.0])
    band = LineString(np.column_stack([xs, ys])).buffer(WALL_THK / 2.0,
                                                        cap_style=2, join_style=1)
    w = trimesh.creation.extrude_polygon(band, height=WALL_H + EMBED)
    w.apply_translation([0, y0, BASE_H - EMBED])
    return w


def text_mesh(s, size, height):
    """Centered extruded mesh of string s, z in [0, height].

    Bold MONOSPACE font for legibility. Each counter (the enclosed hole of 0/4/6/8/9) gets a
    thin STENCIL BRIDGE down through the glyph bottom, so a full-depth THROUGH cut leaves no
    detached island (the counter stays joined to the surrounding base)."""
    tp = TextPath((0, 0), s, size=size, prop=FontProperties(family='DejaVu Sans Mono', weight='bold'))
    rings = [ShPoly(p) for p in tp.to_polygons() if len(p) >= 3]
    rings.sort(key=lambda r: r.area, reverse=True)
    used = [False] * len(rings); parts = []
    for i, o in enumerate(rings):
        if used[i]:
            continue
        holes = []
        for j in range(i + 1, len(rings)):
            if not used[j] and o.contains(rings[j]):
                holes.append(rings[j].exterior.coords); used[j] = True
        parts.append(ShPoly(o.exterior.coords, holes)); used[i] = True
    # stencil-ize: notch the bottom stroke under each counter so the island stays connected
    bridged = []
    for p in parts:
        if p.interiors:
            y_bot = p.bounds[1]
            bars = []
            for h in p.interiors:
                hc = ShPoly(h).centroid
                bars.append(ShPoly([(hc.x - BRIDGE_W / 2, y_bot - 1.0),
                                    (hc.x + BRIDGE_W / 2, y_bot - 1.0),
                                    (hc.x + BRIDGE_W / 2, hc.y),
                                    (hc.x - BRIDGE_W / 2, hc.y)]))
            p = p.buffer(0).difference(unary_union(bars).buffer(0))
        bridged.append(p)
    poly = unary_union(bridged)
    geoms = list(poly.geoms) if poly.geom_type == 'MultiPolygon' else [poly]
    m = trimesh.util.concatenate(
        [trimesh.creation.extrude_polygon(g, height=height) for g in geoms])
    c = m.bounds.mean(axis=0); m.apply_translation([-c[0], -c[1], 0]); return m


def number_cutter(y0, pa):
    """Cutter solid for the PA value, FULL (through) cut in the base beside the wall.

    Flat number in the model XY plane (reads upright off the part), cut clean through the whole
    base for a high-contrast, legible void. Spans z [-CUT_OVER, BASE_H+CUT_OVER]."""
    t = text_mesh(f"{pa:.2f}", NUM_H, BASE_H + 2 * CUT_OVER)   # z in [0, BASE_H+2*CUT_OVER]
    t.apply_scale([NUM_WIDTH_SCALE, 1.0, 1.0])                 # 50% wider in X (centered at origin)
    xc = WALL_LEN + NUM_GAP + NUM_W / 2.0
    t.apply_translation([xc, y0 + PROV_Y / 2.0, -CUT_OVER])
    return t


def build(out):
    ny = (len(PA_VALUES) - 1) * PITCH + PROV_Y
    base = trimesh.creation.box(extents=[RIB_X, ny, BASE_H])
    base.apply_translation([RIB_X / 2.0, ny / 2.0, BASE_H / 2.0])
    parts = [base]
    cutters = []
    for i, pa in enumerate(PA_VALUES):
        y0 = i * PITCH
        parts.append(wall(y0))
        cutters.append(number_cutter(y0, pa))
    asset = trimesh.boolean.union(parts, engine='manifold')
    asset = trimesh.boolean.difference([asset, trimesh.util.concatenate(cutters)], engine='manifold')
    asset.export(out)
    dims = np.round(asset.extents, 2)
    ncomp = len(asset.split(only_watertight=False))
    print(f"[straight blades + base-cut numbers] PA {PA_VALUES[0]:.2f}->{PA_VALUES[-1]:.2f}  "
          f"PITCH={PITCH:.3f} (C++ contract: provino i at designed-Y = i*{PITCH:.3f})")
    print(f"   bbox(X,Y,Z)={dims.tolist()}  watertight={asset.is_watertight}  components={ncomp}  "
          f"read~{WALL_H/VPITCH:.0f} belt layers  "
          f"min(Y+Z)={(asset.vertices[:,1]+asset.vertices[:,2]).min():.3f} (keel-first)")
    print(f"   -> {out}")


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, 'belt_pa_tower.stl'))
    a = ap.parse_args()
    build(a.out)
