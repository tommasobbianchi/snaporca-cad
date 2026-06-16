#include "CadDocument.hpp"

#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <stdexcept>

namespace Slic3r {

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
