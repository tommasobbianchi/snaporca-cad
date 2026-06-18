#include "CadDocument.hpp"
#include "SketchConstraints.hpp"

#include <array>

#include <Standard_Failure.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepLib.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace Slic3r {

// ---- helical-thread construction helpers (file-local) ----------------------

// Helix spine on a cylinder (radius/pitch/height) about `axis`, as a wire.
static TopoDS_Wire make_helix_wire(const gp_Ax3& axis, double radius,
                                   double pitch, double height)
{
    Handle(Geom_CylindricalSurface) cyl = new Geom_CylindricalSurface(axis, radius);
    double turns = (pitch > 1e-6) ? (height / pitch) : 1.0;
    // In the surface (u,v) parametrization u is the angle, v the axial height.
    gp_Pnt2d p0(0.0, 0.0);
    gp_Pnt2d p1(2.0 * M_PI * turns, height);
    Handle(Geom2d_TrimmedCurve) seg = GCE2d_MakeSegment(p0, p1);
    TopoDS_Edge e = BRepBuilderAPI_MakeEdge(seg, cyl).Edge();
    BRepLib::BuildCurves3d(e);
    return BRepBuilderAPI_MakeWire(e).Wire();
}

// Triangular axial thread profile (a planar face) placed at the helix start
// (origin + radius*xdir). Spans +-pitch/2 axially; apex offset radially by depth.
// External: apex points outward (crest = radius+depth), base bites inward.
// Internal: apex points inward (crest = radius-depth), base bites outward.
static TopoDS_Face make_thread_profile(const gp_Pnt& origin, const gp_Dir& xdir,
                                       const gp_Dir& zdir, double radius,
                                       double pitch, double depth, bool internal)
{
    gp_Vec vx(xdir), vz(zdir);
    double inner = internal ? (radius + 0.05) : (radius - 0.05); // bite into material
    double crest = internal ? (radius - depth) : (radius + depth);
    gp_Pnt top (origin.XYZ() + (vx * inner).XYZ() + (vz * ( 0.5 * pitch)).XYZ());
    gp_Pnt bot (origin.XYZ() + (vx * inner).XYZ() + (vz * (-0.5 * pitch)).XYZ());
    gp_Pnt apex(origin.XYZ() + (vx * crest).XYZ());
    BRepBuilderAPI_MakePolygon poly(top, bot, apex, Standard_True);
    return BRepBuilderAPI_MakeFace(poly.Wire(), Standard_True).Face();
}

// ---------------------------------------------------------------------------

int CadDocument::add_sketch(SketchShape shape, const SketchPlane& plane,
                            double width, double height, double radius,
                            const std::string& name)
{
    CadFeature f;
    f.type   = CadFeatureType::Sketch;
    f.name   = name;
    f.shape  = shape;
    f.plane  = plane;
    f.width  = width;
    f.height = height;
    f.radius = radius;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_sketch_profile(const SketchProfile& profile, const SketchPlane& plane,
                                    const std::string& name)
{
    CadFeature f;
    f.type    = CadFeatureType::Sketch;
    f.name    = name;
    f.plane   = plane;
    f.profile = profile;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_sketch_entities(const std::vector<SketchEntity>& entities,
                                     const SketchPlane& plane, const std::string& name,
                                     const std::vector<SketchEntityConstraintDef>& constraints)
{
    CadFeature f;
    f.type               = CadFeatureType::Sketch;
    f.name               = name;
    f.plane              = plane;
    f.entities           = entities;
    f.entity_constraints = constraints;   // driving dimensions, solved by solve_sketch_feature
    features.push_back(f);
    return int(features.size()) - 1;
}

namespace {
// Solve Onshape-style constraints on a feature's SketchEntity list (Fase 4.3).
// All entity types participate: Line (P0,P1), Arc (P0,P1,Center), Circle
// (Center), Point (P0). Solved coordinates are written back, with arc angles
// reflowed from the solved center+endpoints.
bool solve_entity_constraints(CadFeature& f)
{
    if (f.entity_constraints.empty()) return true;

    SketchConstraints sc;
    // table[entity][role] -> solver point id, or -1 if that role is unregistered.
    std::vector<std::array<int, 3>> table(f.entities.size(), {-1, -1, -1});
    auto reg = [&](int ei, SketchPointRole role, const Vec2d& p) {
        table[ei][int(role)] = sc.add_point(p.x(), p.y());
    };
    for (size_t i = 0; i < f.entities.size(); ++i) {
        const SketchEntity& e = f.entities[i];
        switch (e.type) {
        case SketchEntity::Type::Line:
            reg(int(i), SketchPointRole::P0, e.p0);
            reg(int(i), SketchPointRole::P1, e.p1);
            break;
        case SketchEntity::Type::Arc:
            reg(int(i), SketchPointRole::P0, e.p0);
            reg(int(i), SketchPointRole::P1, e.p1);
            reg(int(i), SketchPointRole::Center, e.center);
            break;
        case SketchEntity::Type::Circle:
            reg(int(i), SketchPointRole::Center, e.center);
            break;
        case SketchEntity::Type::Point:
            reg(int(i), SketchPointRole::P0, e.p0);
            break;
        }
    }

    auto pid = [&](int ei, SketchPointRole role) -> int {
        if (ei < 0 || ei >= int(table.size())) return -1;
        return table[ei][int(role)];
    };

    for (const SketchEntityConstraintDef& c : f.entity_constraints) {
        switch (c.type) {
        // Point-form: refs A and B name individual entity points.
        case SketchConstraintType::Fix: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.fix_point(a);
            break;
        }
        case SketchConstraintType::Coincident: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.coincident(a, b);
            break;
        }
        case SketchConstraintType::Horizontal: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.horizontal(a, b);
            break;
        }
        case SketchConstraintType::Vertical: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.vertical(a, b);
            break;
        }
        case SketchConstraintType::Distance: {
            int a = pid(c.ea, c.ra), b = pid(c.eb, c.rb);
            if (a >= 0 && b >= 0) sc.distance(a, b, c.value);
            break;
        }
        case SketchConstraintType::LockX: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.lock_x(a, c.value);
            break;
        }
        case SketchConstraintType::LockY: {
            int a = pid(c.ea, c.ra);
            if (a >= 0) sc.lock_y(a, c.value);
            break;
        }
        // Segment-form: ea and eb name whole line segments (their P0->P1).
        case SketchConstraintType::Parallel:
        case SketchConstraintType::Perpendicular:
        case SketchConstraintType::EqualLength: {
            int a0 = pid(c.ea, SketchPointRole::P0), a1 = pid(c.ea, SketchPointRole::P1);
            int b0 = pid(c.eb, SketchPointRole::P0), b1 = pid(c.eb, SketchPointRole::P1);
            if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0) break;
            if (c.type == SketchConstraintType::Parallel)      sc.parallel(a0, a1, b0, b1);
            else if (c.type == SketchConstraintType::Perpendicular) sc.perpendicular(a0, a1, b0, b1);
            else                                               sc.equal_length(a0, a1, b0, b1);
            break;
        }
        case SketchConstraintType::Concentric: {
            int a = pid(c.ea, SketchPointRole::Center);
            int b = pid(c.eb, SketchPointRole::Center);
            if (a >= 0 && b >= 0) sc.coincident(a, b);
            break;
        }
        case SketchConstraintType::Midpoint: {
            int m = pid(c.ea, c.ra);
            int a = pid(c.eb, SketchPointRole::P0);
            int b = pid(c.eb, SketchPointRole::P1);
            if (m >= 0 && a >= 0 && b >= 0) sc.midpoint(m, a, b);
            break;
        }
        case SketchConstraintType::Symmetric: {
            int a  = pid(c.ea, c.ra);
            int b  = pid(c.eb, c.rb);
            int x0 = pid(c.ec, SketchPointRole::P0);
            int x1 = pid(c.ec, SketchPointRole::P1);
            if (a >= 0 && b >= 0 && x0 >= 0 && x1 >= 0) sc.symmetric(a, b, x0, x1);
            break;
        }
        case SketchConstraintType::Angle: {
            int a0 = pid(c.ea, SketchPointRole::P0), a1 = pid(c.ea, SketchPointRole::P1);
            int b0 = pid(c.eb, SketchPointRole::P0), b1 = pid(c.eb, SketchPointRole::P1);
            if (a0 >= 0 && a1 >= 0 && b0 >= 0 && b1 >= 0) sc.angle(a0, a1, b0, b1, c.value);
            break;
        }
        case SketchConstraintType::Radius:
        case SketchConstraintType::Diameter:
            // dimensions: applied in the post-solve pass below, not via the solver.
            break;
        case SketchConstraintType::Tangent: {
            auto in_range = [&](int e){ return e >= 0 && e < (int)f.entities.size(); };
            if (!in_range(c.ea) || !in_range(c.eb)) break;
            const SketchEntity& ea_e = f.entities[c.ea];
            const SketchEntity& eb_e = f.entities[c.eb];
            auto is_round = [](const SketchEntity& e){
                return e.type == SketchEntity::Type::Circle || e.type == SketchEntity::Type::Arc; };
            if (is_round(ea_e) && eb_e.type == SketchEntity::Type::Line) {
                int cen = pid(c.ea, SketchPointRole::Center);
                int l0 = pid(c.eb, SketchPointRole::P0), l1 = pid(c.eb, SketchPointRole::P1);
                if (cen >= 0 && l0 >= 0 && l1 >= 0) sc.point_line_distance(cen, l0, l1, ea_e.radius);
            } else if (is_round(eb_e) && ea_e.type == SketchEntity::Type::Line) {
                int cen = pid(c.eb, SketchPointRole::Center);
                int l0 = pid(c.ea, SketchPointRole::P0), l1 = pid(c.ea, SketchPointRole::P1);
                if (cen >= 0 && l0 >= 0 && l1 >= 0) sc.point_line_distance(cen, l0, l1, eb_e.radius);
            } else if (is_round(ea_e) && is_round(eb_e)) {
                int c0 = pid(c.ea, SketchPointRole::Center), c1 = pid(c.eb, SketchPointRole::Center);
                if (c0 >= 0 && c1 >= 0) sc.distance(c0, c1, ea_e.radius + eb_e.radius);
            }
            break;
        }
        }
    }

    const bool ok = sc.solve();
    // Write solved coordinates back into the participating entities.
    for (size_t i = 0; i < f.entities.size(); ++i) {
        SketchEntity& e = f.entities[i];
        int ip0 = table[i][int(SketchPointRole::P0)];
        int ip1 = table[i][int(SketchPointRole::P1)];
        int ic  = table[i][int(SketchPointRole::Center)];
        if (ip0 >= 0) e.p0     = sc.get_point(ip0);
        if (ip1 >= 0) e.p1     = sc.get_point(ip1);
        if (ic  >= 0) e.center = sc.get_point(ic);

        if (e.type == SketchEntity::Type::Arc && ic >= 0) {
            // Reflow arc angles from solved center + endpoints, preserving the
            // original sweep direction (CCW vs CW).
            const double old_sweep = e.end_angle - e.start_angle;     // signed, original
            double ns = std::atan2(e.p0.y() - e.center.y(), e.p0.x() - e.center.x());
            double ne = std::atan2(e.p1.y() - e.center.y(), e.p1.x() - e.center.x());
            double sweep = ne - ns;
            // Normalize `sweep` into (-2pi, 2pi) then match the sign of old_sweep so
            // the arc keeps turning the same way it did before solving.
            const double TWO_PI = 2.0 * M_PI;
            while (sweep <=  -TWO_PI) sweep += TWO_PI;
            while (sweep >=   TWO_PI) sweep -= TWO_PI;
            if (old_sweep >= 0.0 && sweep < 0.0) sweep += TWO_PI;
            if (old_sweep <  0.0 && sweep > 0.0) sweep -= TWO_PI;
            e.start_angle = ns;
            e.end_angle   = ns + sweep;
            e.radius = 0.5 * ((e.p0 - e.center).norm() + (e.p1 - e.center).norm());
        }
        if (e.type == SketchEntity::Type::Circle && ic >= 0) {
            // p0 mirrors the center for circles; keep them consistent.
            e.p0 = e.center;
        }
    }
    // Apply radius/diameter dimensions directly (radius is not a solver variable).
    for (const auto& c : f.entity_constraints) {
        if (c.type != SketchConstraintType::Radius &&
            c.type != SketchConstraintType::Diameter) continue;
        if (c.ea < 0 || c.ea >= (int)f.entities.size()) continue;
        SketchEntity& e = f.entities[c.ea];
        if (e.type != SketchEntity::Type::Circle && e.type != SketchEntity::Type::Arc) continue;
        const double r = (c.type == SketchConstraintType::Diameter) ? 0.5 * c.value : c.value;
        if (r <= 0.0) continue;
        e.radius = r;
        if (e.type == SketchEntity::Type::Arc) {
            // Rescale endpoints to the new radius around the (solved) center, keeping
            // each endpoint's direction so the reflowed start/end angles stay valid.
            auto rescale = [&](Vec2d& p) {
                Vec2d d = p - e.center;
                const double n = d.norm();
                if (n > 1e-12) p = e.center + (r / n) * d;
            };
            rescale(e.p0);
            rescale(e.p1);
        }
    }
    return ok;
}
} // namespace

bool CadDocument::solve_sketch_feature(int index)
{
    if (index < 0 || index >= int(features.size())) return false;
    CadFeature& f = features[index];
    if (f.type != CadFeatureType::Sketch) return false;

    // Onshape-style entity sketches solve against entity endpoints (Fase 4.2).
    if (!f.entities.empty())
        return solve_entity_constraints(f);

    if (f.constraints.empty()) return true;

    SketchConstraints sc;
    for (const Vec2d& p : f.profile.points)
        sc.add_point(p.x(), p.y());

    for (const SketchConstraintDef& c : f.constraints) {
        switch (c.type) {
        case SketchConstraintType::Fix:          sc.fix_point(c.a); break;
        case SketchConstraintType::Coincident:   sc.coincident(c.a, c.b); break;
        case SketchConstraintType::Horizontal:   sc.horizontal(c.a, c.b); break;
        case SketchConstraintType::Vertical:     sc.vertical(c.a, c.b); break;
        case SketchConstraintType::Distance:     sc.distance(c.a, c.b, c.value); break;
        case SketchConstraintType::LockX:        sc.lock_x(c.a, c.value); break;
        case SketchConstraintType::LockY:        sc.lock_y(c.a, c.value); break;
        case SketchConstraintType::EqualLength:  sc.equal_length(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Parallel:     sc.parallel(c.a, c.b, c.c, c.d); break;
        case SketchConstraintType::Perpendicular:sc.perpendicular(c.a, c.b, c.c, c.d); break;
        }
    }

    const bool ok = sc.solve();
    for (size_t i = 0; i < f.profile.points.size(); ++i)
        f.profile.points[i] = sc.get_point(int(i));
    return ok;
}

int CadDocument::add_extrude(int sketch_ref, double distance, bool symmetric,
                             BooleanMode mode, const std::string& name)
{
    CadFeature f;
    f.type       = CadFeatureType::Extrude;
    f.name       = name;
    f.sketch_ref = sketch_ref;
    f.distance   = distance;
    f.symmetric  = symmetric;
    f.mode       = mode;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_fillet(double radius, FaceGroup faces, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Fillet;
    f.name         = name;
    f.dressup_size = radius;
    f.face_group   = faces;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_chamfer(double distance, FaceGroup faces, const std::string& name)
{
    CadFeature f;
    f.type         = CadFeatureType::Chamfer;
    f.name         = name;
    f.dressup_size = distance;
    f.face_group   = faces;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_hole(double diameter, double depth, bool through,
                          double x, double y, const SketchPlane& plane,
                          const std::string& name)
{
    CadFeature f;
    f.type          = CadFeatureType::Hole;
    f.name          = name;
    f.plane         = plane;
    f.hole_diameter = diameter;
    f.hole_depth    = depth;
    f.hole_through  = through;
    f.hole_x        = x;
    f.hole_y        = y;
    features.push_back(f);
    return int(features.size()) - 1;
}

int CadDocument::add_thread(double radius, double pitch, double height, double depth,
                            bool internal, double x, double y, const SketchPlane& plane,
                            const std::string& name)
{
    CadFeature f;
    f.type            = CadFeatureType::Thread;
    f.name            = name;
    f.plane           = plane;
    f.thread_radius   = radius;
    f.thread_pitch    = pitch;
    f.thread_height   = height;
    f.thread_depth    = depth;
    f.thread_internal = internal;
    f.thread_x        = x;
    f.thread_y        = y;
    features.push_back(f);
    return int(features.size()) - 1;
}

void CadDocument::clear()
{
    features.clear();
    body = TopoDS_Shape();
    display_mesh = TriangleMesh{};
    error.clear();
}

// Re-run recompute(); if it fails for a GENUINE geometry error, restore `snapshot`
// and recompute that instead, so a rejected edit leaves the document exactly as it
// was. recompute() also returns false for the BENIGN case where the edit simply
// leaves no solid-producing feature (empty document, or only a sketch) — that is a
// valid result of a deletion, not a failure, so we accept it with an empty body.
static bool commit_or_rollback(CadDocument& doc, std::vector<CadFeature>& snapshot)
{
    if (doc.recompute())
        return true;

    bool has_solid_feature = false;
    for (const auto& f : doc.features)
        if (f.enabled && f.type != CadFeatureType::Sketch) { has_solid_feature = true; break; }
    if (!has_solid_feature) {
        doc.body         = TopoDS_Shape();
        doc.display_mesh = TriangleMesh{};
        doc.error.clear();
        return true;
    }

    std::string fail_err = doc.error;   // why the attempted edit failed
    doc.features.swap(snapshot);
    doc.recompute();                    // restore the previous good body (clears error)
    doc.error = fail_err.empty() ? std::string("feature is used by a later feature")
                                 : fail_err;
    return false;
}

bool CadDocument::remove_feature(int index)
{
    if (index < 0 || index >= int(features.size()))
        return false;

    std::vector<CadFeature> snapshot = features;

    // Deleting a Sketch cascades to every Extrude that consumes it (a dangling
    // Extrude would have no wire). A lone Sketch, by contrast, is harmless.
    std::vector<int> remove{index};
    if (features[index].type == CadFeatureType::Sketch) {
        for (int j = 0; j < int(features.size()); ++j)
            if (features[j].type == CadFeatureType::Extrude && features[j].sketch_ref == index)
                remove.push_back(j);
    }
    std::sort(remove.begin(), remove.end());
    remove.erase(std::unique(remove.begin(), remove.end()), remove.end());

    // Erase high-to-low so earlier indices stay valid.
    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
        features.erase(features.begin() + *it);

    // Remap surviving sketch_ref through the deletions: subtract the count of
    // removed indices that sat before it; orphaned refs (target removed) -> -1.
    for (auto& f : features) {
        if (f.type != CadFeatureType::Extrude || f.sketch_ref < 0)
            continue;
        if (std::binary_search(remove.begin(), remove.end(), f.sketch_ref)) {
            f.sketch_ref = -1;
        } else {
            int shift = 0;
            for (int r : remove)
                if (r < f.sketch_ref) ++shift;
            f.sketch_ref -= shift;
        }
    }

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::move_feature(int index, int delta)
{
    if (index < 0 || index >= int(features.size()))
        return false;
    int target = index + delta;
    if (target < 0 || target >= int(features.size()))
        return true; // clamped at the ends — no-op, not a failure

    std::vector<CadFeature> snapshot = features;
    std::swap(features[index], features[target]);

    // The two slots traded places: fix any sketch_ref that pointed at either.
    for (auto& f : features) {
        if (f.type != CadFeatureType::Extrude) continue;
        if (f.sketch_ref == index)       f.sketch_ref = target;
        else if (f.sketch_ref == target) f.sketch_ref = index;
    }

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::replace_feature(int index, const CadFeature& edited)
{
    if (index < 0 || index >= int(features.size()))
        return false;

    std::vector<CadFeature> snapshot = features;

    // Preserve identity (name) and the structural link (sketch_ref) from the
    // original; only the user-editable parameters come from `edited`.
    CadFeature f      = edited;
    f.name            = features[index].name;
    f.type            = features[index].type;
    if (f.type == CadFeatureType::Extrude)
        f.sketch_ref  = features[index].sketch_ref;
    features[index]   = f;

    return commit_or_rollback(*this, snapshot);
}

bool CadDocument::replace_sketch_extrude(int sketch_idx, int extrude_idx,
                                         const CadFeature& edited)
{
    if (sketch_idx  < 0 || sketch_idx  >= int(features.size())) return false;
    if (extrude_idx < 0 || extrude_idx >= int(features.size())) return false;

    std::vector<CadFeature> snapshot = features;

    // A box in the tree is two linked features: the Sketch consumes the profile
    // params (shape/plane/width/height/radius), the Extrude consumes the solid
    // params (distance/symmetric/mode). `edited` carries all of them; split it
    // back into the two slots, preserving each slot's name/type and the link.
    CadFeature& sk = features[sketch_idx];
    sk.shape  = edited.shape;
    sk.plane  = edited.plane;
    sk.width  = edited.width;
    sk.height = edited.height;
    sk.radius = edited.radius;

    CadFeature& ex = features[extrude_idx];
    ex.distance  = edited.distance;
    ex.symmetric = edited.symmetric;
    ex.mode      = edited.mode;

    return commit_or_rollback(*this, snapshot);
}

TopoDS_Wire CadDocument::build_sketch_wire(const CadFeature& sketch) const
{
    if (!sketch.entities.empty()) {
        TopoDS_Wire w = SketchEngine::entities_to_wire(sketch.entities, sketch.plane);
        if (!w.IsNull()) return w;
        // fall through to legacy paths if entities produced nothing
    }
    if (!sketch.profile.points.empty()) {
        SketchProfile prof = sketch.profile;
        prof.closed = true;               // extrude needs a closed wire
        TopoDS_Wire w = prof.to_occt_wire(sketch.plane);
        if (w.IsNull()) throw std::runtime_error("sketch profile wire failed");
        return w;
    }
    if (sketch.shape == SketchShape::Circle) {
        gp_Pnt o(sketch.plane.origin.x(), sketch.plane.origin.y(), sketch.plane.origin.z());
        gp_Dir n(sketch.plane.normal.x(), sketch.plane.normal.y(), sketch.plane.normal.z());
        gp_Circ circ(gp_Ax2(o, n), sketch.radius);
        TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circ).Edge();
        BRepBuilderAPI_MakeWire wm(e);
        if (!wm.IsDone()) throw std::runtime_error("circle wire failed");
        return wm.Wire();
    }
    // Rectangle centered on the plane origin
    SketchProfile prof;
    double hw = sketch.width * 0.5, hh = sketch.height * 0.5;
    prof.points.push_back(Vec2d(-hw, -hh));
    prof.points.push_back(Vec2d( hw, -hh));
    prof.points.push_back(Vec2d( hw,  hh));
    prof.points.push_back(Vec2d(-hw,  hh));
    prof.closed = true;
    return prof.to_occt_wire(sketch.plane);
}

void CadDocument::apply_feature(TopoDS_Shape& result, bool& have_body, const CadFeature& f) const
{
    switch (f.type) {
    case CadFeatureType::Sketch:
        return; // sketches carry no solid; consumed by an extrude
    case CadFeatureType::Extrude: {
        // Use the referenced sketch when sketch_ref is a valid Sketch index,
        // otherwise fall back to f's own inline sketch params (this makes a
        // single self-contained candidate previewable).
        const CadFeature& sk = (f.sketch_ref >= 0 && f.sketch_ref < int(features.size())
                                && features[f.sketch_ref].type == CadFeatureType::Sketch)
                               ? features[f.sketch_ref] : f;
        TopoDS_Wire  wire = build_sketch_wire(sk);
        TopoDS_Shape tool = SketchEngine::make_extrude(wire, sk.plane, f.distance, f.symmetric, 0.0);
        if (!have_body || f.mode == BooleanMode::New) {
            result = tool;
            have_body = true;
        } else if (f.mode == BooleanMode::Add) {
            BRepAlgoAPI_Fuse fuse(result, tool);
            if (!fuse.IsDone()) throw std::runtime_error("fuse failed");
            result = fuse.Shape();
        } else { // Cut
            BRepAlgoAPI_Cut cut(result, tool);
            if (!cut.IsDone()) throw std::runtime_error("cut failed");
            result = cut.Shape();
        }
        break;
    }
    case CadFeatureType::Fillet:
        if (!have_body) throw std::runtime_error("fillet needs a body");
        result = GeometryEngine::apply_fillet(result, f.dressup_size, f.face_group);
        break;
    case CadFeatureType::Chamfer:
        if (!have_body) throw std::runtime_error("chamfer needs a body");
        result = GeometryEngine::apply_chamfer(result, f.dressup_size, f.face_group);
        break;
    case CadFeatureType::Hole: {
        if (!have_body) throw std::runtime_error("hole needs a body");
        // Circle wire centered at the positioned point on the plane
        Vec3d c = f.plane.to_world(Vec2d(f.hole_x, f.hole_y));
        gp_Pnt o(c.x(), c.y(), c.z());
        gp_Dir n(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z());
        gp_Circ circ(gp_Ax2(o, n), f.hole_diameter * 0.5);
        TopoDS_Edge e = BRepBuilderAPI_MakeEdge(circ).Edge();
        BRepBuilderAPI_MakeWire wm(e);
        if (!wm.IsDone()) throw std::runtime_error("hole wire failed");
        // Through = symmetric huge cut (passes fully through any body);
        // Blind = +normal extrude of hole_depth into the body.
        TopoDS_Shape tool = f.hole_through
            ? SketchEngine::make_extrude(wm.Wire(), f.plane, 1.0e5, true, 0.0)
            : SketchEngine::make_extrude(wm.Wire(), f.plane, f.hole_depth, false, 0.0);
        BRepAlgoAPI_Cut cut(result, tool);
        if (!cut.IsDone()) throw std::runtime_error("hole cut failed");
        result = cut.Shape();
        break;
    }
    case CadFeatureType::Thread: {
        // Axis at the positioned point on the plane; +normal = thread rise.
        Vec3d c3 = f.plane.to_world(Vec2d(f.thread_x, f.thread_y));
        gp_Pnt  c(c3.x(), c3.y(), c3.z());
        gp_Dir  zdir(f.plane.normal.x(), f.plane.normal.y(), f.plane.normal.z());
        gp_Dir  xdir(f.plane.x_axis.x(), f.plane.x_axis.y(), f.plane.x_axis.z());
        gp_Ax3  ax3(c, zdir, xdir);
        gp_Ax2  ax2(c, zdir, xdir);

        // Build the swept helical ridge (guarded — never fatal).
        TopoDS_Shape ridge;
        bool have_ridge = false;
        try {
            TopoDS_Wire spine = make_helix_wire(ax3, f.thread_radius,
                                                f.thread_pitch, f.thread_height);
            TopoDS_Face prof  = make_thread_profile(c, xdir, zdir, f.thread_radius,
                                                    f.thread_pitch, f.thread_depth,
                                                    f.thread_internal);
            BRepOffsetAPI_MakePipe pipe(spine, prof);
            pipe.Build();
            if (pipe.IsDone()) {
                ridge = pipe.Shape();
                have_ridge = !ridge.IsNull();
            }
        } catch (const std::exception&) {
            have_ridge = false; // fall back to the bare cylinder/bore below
        }

        if (f.thread_internal) {
            if (!have_body) throw std::runtime_error("internal thread needs a body");
            // Tapped bore: cut a cylinder, then carve the inward helical ridge.
            TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(ax2, f.thread_radius,
                                                         f.thread_height).Shape();
            BRepAlgoAPI_Cut cut_bore(result, bore);
            if (!cut_bore.IsDone()) throw std::runtime_error("thread bore cut failed");
            result = cut_bore.Shape();
            if (have_ridge) {
                BRepAlgoAPI_Cut cut_ridge(result, ridge);
                if (cut_ridge.IsDone()) result = cut_ridge.Shape();
            }
        } else {
            // External threaded rod = a New body: base cylinder + fused ridge.
            TopoDS_Shape rod = BRepPrimAPI_MakeCylinder(ax2, f.thread_radius,
                                                        f.thread_height).Shape();
            if (have_ridge) {
                BRepAlgoAPI_Fuse fuse(rod, ridge);
                if (fuse.IsDone()) rod = fuse.Shape();
            }
            result = rod;
            have_body = true;
        }
        break;
    }
    }
}

bool CadDocument::recompute()
{
    error.clear();
    TopoDS_Shape result;
    bool have_body = false;
    try {
        for (const CadFeature& f : features) {
            if (!f.enabled) continue;
            if (f.type == CadFeatureType::Sketch) continue; // consumed by an extrude
            apply_feature(result, have_body, f);
        }
    } catch (const Standard_Failure& e) {
        // OCCT raises Standard_Failure (NOT a std::exception) — must be caught
        // here or it escapes the event handler and terminates the app.
        error = e.GetMessageString() ? e.GetMessageString() : "OCCT operation failed";
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown geometry error";
        return false;
    }
    if (!have_body) { error = "no solid-producing features"; return false; }

    body = result;
    display_mesh = SketchEngine::tessellate(body, linear_deflection, angular_deflection);
    if (display_mesh.its.indices.empty()) {
        error = "tessellation produced an empty mesh";
        return false;
    }
    return true;
}

bool CadDocument::preview(const CadFeature& candidate, TriangleMesh& out_mesh, std::string& err) const
{
    err.clear();
    TopoDS_Shape result = body;            // start from the current committed body
    bool have_body = !body.IsNull();
    try {
        apply_feature(result, have_body, candidate);
    } catch (const Standard_Failure& e) {
        err = e.GetMessageString() ? e.GetMessageString() : "OCCT operation failed";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    } catch (...) {
        err = "unknown geometry error";
        return false;
    }
    if (!have_body || result.IsNull()) {
        err = "preview produced no geometry";
        return false;
    }
    out_mesh = SketchEngine::tessellate(result, linear_deflection, angular_deflection);
    if (out_mesh.its.indices.empty()) {
        err = "preview produced an empty mesh";
        return false;
    }
    return true;
}

} // namespace Slic3r
