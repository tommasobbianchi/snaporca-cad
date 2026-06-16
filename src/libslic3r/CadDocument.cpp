#include "CadDocument.hpp"

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

TopoDS_Wire CadDocument::build_sketch_wire(const CadFeature& sketch) const
{
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

bool CadDocument::recompute()
{
    error.clear();
    TopoDS_Shape result;
    bool have_body = false;
    try {
        for (const CadFeature& f : features) {
            if (!f.enabled) continue;
            switch (f.type) {
            case CadFeatureType::Sketch:
                continue; // consumed by an extrude via sketch_ref
            case CadFeatureType::Extrude: {
                if (f.sketch_ref < 0 || f.sketch_ref >= int(features.size()))
                    throw std::runtime_error("extrude references an invalid sketch");
                const CadFeature& sk = features[f.sketch_ref];
                if (sk.type != CadFeatureType::Sketch)
                    throw std::runtime_error("extrude reference is not a sketch");

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
    } catch (const std::exception& e) {
        error = e.what();
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

} // namespace Slic3r
