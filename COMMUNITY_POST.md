# Community post — SnapOrca Design

**Repo:** https://github.com/tommasobbianchi/snaporca-cad
**Innovation Fund:** submitted to Phase 1 (Slicer / software)

---

## Title options
- 🐳 I built a full parametric CAD environment *inside* Snapmaker Orca — design → slice → print, no round-trips
- SnapOrca Design: sketch-first CAD living right inside your slicer (Innovation Fund entry)
- From blank bed to printed part without ever leaving Orca — a CAD tab for Snapmaker Orca

---

## Body (Reddit / Forum / Facebook)

Hey makers! 👋

I got tired of the endless relay race — model in CAD, export an STL, re-import into the slicer, spot a wall that's 0.3 mm too thin, jump back to CAD, repeat. So I built **SnapOrca Design**: a real, parametric, sketch-first CAD environment that lives **directly inside Snapmaker Orca**. The thing you design and the thing you print are never more than a click apart.

![SnapOrca Design demo](https://raw.githubusercontent.com/tommasobbianchi/snaporca-cad/feature/cad-primitives/snaporca-demo.gif)

*(blank bed → hexagon sketch → extrude → fillet → hole → finished solid, all inside Orca)*

### What it does
- **Sketch-first, Onshape/SolidWorks-style** — draw a constrained 2D sketch, then grow it into a solid. A genuine geometric **constraint solver** (SolveSpace) keeps geometry honest, with a live degrees-of-freedom readout.
- **Draw-then-edit** — sketch a circle and a value field pops up right where you're looking; type the exact radius, Enter, done. Every 2D tool works this way.
- **Industrial B-rep kernel** — powered by **OpenCASCADE**, so true solids, clean booleans, and **STEP import as fully editable B-rep**.
- **The full solid-feature timeline** — extrude, revolve, sweep, loft, linear/circular pattern, hole, thread, shell, draft, fillet, chamfer, boolean, plane-cut, datum planes — each with an in-canvas gizmo.
- **Text & SVG** import as crisp, extrudable geometry.
- **Multi-body** modeling, per-body colors, move/rotate gizmos, place-on-face.
- Rides the **native Orca viewport** — same bed, camera, view cube, and it follows your light/dark theme.

The whole point: **the shortest path from idea to printed object** on your Snapmaker. Think it → sketch it → shape it → slice it → print it, without ever breaking flow.

It's **open-source** (AGPL-3.0, built as an Orca integration) and I just submitted it to the **Snapmaker U1 Innovation Fund** (Phase 1).

### Try it / help it grow
- ⭐ **Star the repo** if you'd use this: https://github.com/tommasobbianchi/snaporca-cad
- 🐛 Issues, ideas, and PRs very welcome — there's a known list (extrude depth label click-edit, per-letter text selection) I'm actively working through.
- 💬 Honest feedback is gold — tell me what feature you'd want first.

Thanks for taking a look — would love to hear what you'd build with it. 🚀

---

## Short version (Discord / X)

🐳 **SnapOrca Design** — I put a full parametric, sketch-first CAD tab *inside* Snapmaker Orca. Sketch → constrain → extrude/revolve/loft/pattern/etc → slice → print, no CAD↔slicer round-trips. OpenCASCADE kernel, SolveSpace constraints, STEP import, in-canvas gizmos. Open-source (AGPL-3.0), Innovation Fund Phase 1 entry. ⭐ https://github.com/tommasobbianchi/snaporca-cad
