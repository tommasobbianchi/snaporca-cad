# SnapOrca Design — CAD, born inside your slicer 🚀

**Design it. Slice it. Print it. All in one window — no round-trips, no exports, no friction.**

Most 3D-printing workflows are a relay race: model in one app, export an STL, re-import into a
slicer, discover the wall is 0.3 mm too thin, jump *back* to the CAD tool, and run the whole gauntlet
again. SnapOrca Design ends that relay. We embedded a real, parametric, sketch-first CAD environment
**directly inside Snapmaker_Orca** — so the thing you design and the thing you print are never more
than a click apart. ✨

---

## Why it's different

🧩 **One app, zero hand-offs.** Sketch a profile, extrude it, fillet an edge, and hit *Slice* —
without ever leaving the slicer. The model flows straight onto the build plate you're already looking
at, on the *real* printer bed you're about to print on.

🎯 **Sketch-first, just like the pros.** SnapOrca speaks the Onshape/SolidWorks dialect: draw a
constrained 2D sketch, then grow it into a solid. A genuine geometric **constraint solver**
(SolveSpace) keeps your geometry honest — horizontal stays horizontal, tangent stays tangent, and the
degrees-of-freedom readout tells you exactly when a sketch is fully nailed down. ✅

✍️ **Draw-then-edit magic.** Sketch a circle and a value field pops up *right where you're looking*,
ready for the exact radius. Type `50`, press Enter, done. Every 2D tool works this way — your hand
never leaves the canvas to go hunting through a side panel. And the feature you're editing now lights
up in amber, so you always know which number changes what. 💡

🛠️ **A real B-rep kernel under the hood.** Powered by **OpenCASCADE**, the same industrial-grade
geometry kernel behind professional CAD. That means true solids, crisp edges, watertight booleans —
and **STEP import as fully editable B-rep**, so you can pull in a supplier's part and keep modeling.

---

## The toolbox 🧰

**2D sketching** — line, polyline, rectangle (corner / center / oblique / rounded), circle (center /
2-point / 3-point), arc (3-point / tangent / center), slot, arc-slot, polygon, ellipse, ellipse-arc,
B-spline, points… plus **live constraints**, smart **auto-inference** (loops self-close, edges snap
horizontal/vertical automatically), full **dimensioning**, and on-canvas **trim / extend / mirror /
offset / fillet** scissors.

**Text & art** — drop in **Text** and **SVG** artwork as crisp, extrudable geometry. Badge it,
emboss it, cut it out. 🎨

**Solid features — the full timeline:**
- 🔼 **Extrude** (blind, symmetric, two-sided, through-all, up-to-face, with taper & boolean modes)
- 🔄 **Revolve** with a drag-to-sweep angle gizmo
- 🌀 **Sweep** and **Loft / ThruSections**
- 🔁 **Pattern** — linear *and* circular, with in-canvas spacing/angle handles
- 🕳️ **Hole**, 🔩 **Thread**, 🫙 **Shell**, 📐 **Draft**, 🟢 **Fillet** & **Chamfer**
- ✂️ **Plane Cut**, 🔗 **Booleans** (union / subtract / intersect)
- 📄 **Datum planes** for sketching anywhere in space

**Direct & multi-body modeling** — every new extrude can be its own coexisting solid, each with its
own colour, visibility, and **in-canvas move/rotate gizmo**. **Place-on-Face** lays any picked face
flat in one keystroke. Pick a face, sketch on it, cut into it — topology-aware, the way modern CAD
should be.

**Visual everything.** Every feature has an **in-canvas gizmo** — drag an arrow to set a depth, grab
a ring to rotate, tug a handle to round an edge. Numbers when you want them, direct manipulation when
you don't. 🖐️

---

## Built for makers, by a maker ❤️

SnapOrca Design isn't a toy CAD bolted onto a slicer — it's a serious modeling environment that
*lives* where your prints are born. It rides on the native Orca viewport (same camera, same bed, same
view cube you already know), follows your light/dark theme, and keeps the whole experience fast and
familiar.

The dream is simple: **the shortest possible path from idea to printed object.** Think it, sketch it,
shape it, slice it, print it — without ever breaking flow. 🌟

*That's SnapOrca Design. Let's build the future of "from screen to bed" — together.* 🐳🔥
