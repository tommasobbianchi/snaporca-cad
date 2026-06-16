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
            if (f.type != CadFeatureType::Extrude) continue;
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
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (!have_body) { error = "no extrude features"; return false; }

    body = result;
    display_mesh = SketchEngine::tessellate(body, linear_deflection, angular_deflection);
    if (display_mesh.its.indices.empty()) {
        error = "tessellation produced an empty mesh";
        return false;
    }
    return true;
}

} // namespace Slic3r
