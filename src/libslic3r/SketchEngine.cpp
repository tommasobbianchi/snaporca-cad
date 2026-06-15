#include "SketchEngine.hpp"

#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <GeomAPI_IntCS.hxx>
#include <stdexcept>

namespace Slic3r {

// ---- SketchPlane ----

gp_Pln SketchPlane::to_occt() const
{
    gp_Pnt o(origin.x(), origin.y(), origin.z());
    gp_Dir n(normal.x(), normal.y(), normal.z());
    gp_Dir x(x_axis.x(), x_axis.y(), x_axis.z());
    return gp_Pln(gp_Ax3(o, n, x));
}

SketchPlane SketchPlane::from_face(const TopoDS_Face& face)
{
    SketchPlane sp;
    // Use the first triangulation vertex + face normal
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (!tri.IsNull() && tri->NbNodes() > 0) {
        gp_Pnt p = tri->Node(1).Transformed(loc.Transformation());
        sp.origin = Vec3d(p.X(), p.Y(), p.Z());
    }
    // Compute normal from BRep face
    BRepAdaptor_Surface surf(face);
    if (surf.GetType() != GeomAbs_Plane) {
        // Fallback: try to use the first uv point
        double u1 = surf.FirstUParameter(), v1 = surf.FirstVParameter();
        gp_Pnt pt; gp_Vec du, dv;
        surf.D1(u1, v1, pt, du, dv);
        gp_Dir n = gp_Dir(du.Crossed(dv));
        sp.normal = Vec3d(n.X(), n.Y(), n.Z());
        sp.x_axis = Vec3d(du.X(), du.Y(), du.Z()).normalized();
        sp.y_axis = sp.normal.cross(sp.x_axis).normalized();
        return sp;
    }
    // Plane face — use the plane directly
    gp_Pln pln = surf.Plane();
    sp.origin = Vec3d(pln.Location().X(), pln.Location().Y(), pln.Location().Z());
    gp_Dir n = pln.Axis().Direction();
    sp.normal = Vec3d(n.X(), n.Y(), n.Z());
    gp_Dir xd = pln.XAxis().Direction();
    sp.x_axis = Vec3d(xd.X(), xd.Y(), xd.Z());
    sp.y_axis = sp.normal.cross(sp.x_axis).normalized();
    return sp;
}

Vec2d SketchPlane::project(const Vec3d& ray_origin, const Vec3d& ray_dir) const
{
    double denom = ray_dir.dot(normal);
    if (std::abs(denom) < 1e-12)
        return {0, 0}; // ray parallel to plane
    double t = (origin - ray_origin).dot(normal) / denom;
    if (t < 0)
        return {0, 0}; // behind camera
    Vec3d hit = ray_origin + t * ray_dir;
    Vec3d local = hit - origin;
    return {local.dot(x_axis), local.dot(y_axis)};
}

Vec3d SketchPlane::to_world(const Vec2d& pt) const
{
    return origin + x_axis * pt.x() + y_axis * pt.y();
}

// ---- SketchProfile ----

bool SketchProfile::is_closed(double tolerance) const
{
    if (points.size() < 3) return false;
    return (points.front() - points.back()).norm() < tolerance;
}

bool SketchProfile::try_close(double tolerance)
{
    if (is_closed(tolerance)) {
        closed = true;
        return true;
    }
    if (points.size() < 2) return false;
    if ((points.front() - points.back()).norm() < tolerance) {
        closed = true;
        return true;
    }
    return false;
}

TopoDS_Wire SketchProfile::to_occt_wire(const SketchPlane& plane) const
{
    if (points.size() < 2)
        throw std::runtime_error("Profile has fewer than 2 points");

    BRepBuilderAPI_MakeWire builder;
    for (size_t i = 0; i < points.size(); ++i) {
        Vec3d a3 = plane.to_world(points[i]);
        Vec3d b3 = plane.to_world(points[(i + 1) % points.size()]);
        gp_Pnt pa(a3.x(), a3.y(), a3.z());
        gp_Pnt pb(b3.x(), b3.y(), b3.z());
        builder.Add(BRepBuilderAPI_MakeEdge(pa, pb).Edge());
    }
    builder.Build();
    if (!builder.IsDone())
        throw std::runtime_error("Failed to build wire from profile");
    return builder.Wire();
}

// ---- SketchEngine ----

static TopoDS_Shape extrude_face_internal(const TopoDS_Face& face, const gp_Dir& dir, double length, bool symmetric)
{
    gp_Vec vec = gp_Vec(dir) * length;
    if (symmetric) {
        gp_Vec halfVec = gp_Vec(dir) * (length / 2.0);
        BRepPrimAPI_MakePrism pos(face, halfVec);
        BRepPrimAPI_MakePrism neg(face, -halfVec);
        if (!pos.IsDone() || !neg.IsDone()) throw std::runtime_error("Symmetric extrude failed");
        BRepAlgoAPI_Fuse fuse(pos.Shape(), neg.Shape());
        if (!fuse.IsDone()) throw std::runtime_error("Fuse failed");
        return fuse.Shape();
    }
    BRepPrimAPI_MakePrism prism(face, vec);
    if (!prism.IsDone()) throw std::runtime_error("Extrude failed");
    return prism.Shape();
}

TopoDS_Shape SketchEngine::make_extrude(const TopoDS_Wire& wire, const SketchPlane& plane,
                                        double length, bool symmetric, double /*taper_deg*/)
{
    BRepBuilderAPI_MakeFace fm(wire);
    if (!fm.IsDone()) throw std::runtime_error("Failed to make face from wire");
    gp_Dir dir(plane.normal.x(), plane.normal.y(), plane.normal.z());
    return extrude_face_internal(fm.Face(), dir, length, symmetric);
}

TopoDS_Shape SketchEngine::make_extrude_face(const TopoDS_Face& face, const SketchPlane& plane,
                                             double length, bool symmetric, double /*taper_deg*/)
{
    gp_Dir dir(plane.normal.x(), plane.normal.y(), plane.normal.z());
    return extrude_face_internal(face, dir, length, symmetric);
}

TopoDS_Shape SketchEngine::make_revolve(const TopoDS_Wire& wire, const SketchPlane& plane,
                                        double angle_deg)
{
    BRepBuilderAPI_MakeFace faceMaker(wire);
    if (!faceMaker.IsDone())
        throw std::runtime_error("Failed to make face from wire");
    TopoDS_Face face = faceMaker.Face();

    // Revolve around the X-axis of the sketch plane
    gp_Pnt o(plane.origin.x(), plane.origin.y(), plane.origin.z());
    gp_Dir xd(plane.x_axis.x(), plane.x_axis.y(), plane.x_axis.z());
    gp_Ax1 axis(o, xd);

    double angle_rad = angle_deg * M_PI / 180.0;
    BRepPrimAPI_MakeRevol rev(face, axis, angle_rad);
    if (!rev.IsDone())
        throw std::runtime_error("Failed to revolve");
    return rev.Shape();
}

TopoDS_Shape SketchEngine::make_pocket(const TopoDS_Wire& wire, const SketchPlane& plane,
                                        const TopoDS_Shape& target, double depth)
{
    BRepBuilderAPI_MakeFace fm(wire);
    if (!fm.IsDone()) throw std::runtime_error("Pocket face failed");
    TopoDS_Shape tool = extrude_face_internal(fm.Face(),
        gp_Dir(plane.normal.x(), plane.normal.y(), plane.normal.z()), depth + 1.0, false);
    BRepAlgoAPI_Cut cut(target, tool);
    if (!cut.IsDone()) throw std::runtime_error("Pocket cut failed");
    return cut.Shape();
}

TriangleMesh SketchEngine::tessellate(const TopoDS_Shape& shape,
                                      double linear_deflection,
                                      double angular_deflection)
{
    BRepMesh_IncrementalMesh mesh(shape, linear_deflection, false, angular_deflection, true);

    int nbNodes = 0, nbTriangles = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(exp.Current()), loc);
        if (!tri.IsNull()) {
            nbNodes += tri->NbNodes();
            nbTriangles += tri->NbTriangles();
        }
    }

    if (nbTriangles == 0 || nbNodes == 0)
        return TriangleMesh{};

    stl_file stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = static_cast<uint32_t>(nbTriangles);
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);

    std::vector<Vec3f> pts;
    pts.reserve(nbNodes);

    int nodeOff = 0, triOff = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Shape& aFace = exp.Current();
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(aFace), loc);
        if (tri.IsNull()) continue;

        gp_Trsf trsf = loc.Transformation();
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            gp_Pnt p = tri->Node(i);
            p.Transform(trsf);
            pts.emplace_back(Vec3f(p.X(), p.Y(), p.Z()));
        }

        TopAbs_Orientation orient = exp.Current().Orientation();
        int ids[3];
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            Poly_Triangle t = tri->Triangle(i);
            t.Get(ids[0], ids[1], ids[2]);
            if (orient == TopAbs_REVERSED)
                std::swap(ids[1], ids[2]);

            stl_facet facet;
            facet.vertex[0] = pts[ids[0] + nodeOff - 1].cast<float>();
            facet.vertex[1] = pts[ids[1] + nodeOff - 1].cast<float>();
            facet.vertex[2] = pts[ids[2] + nodeOff - 1].cast<float>();
            facet.extra[0] = 0;
            facet.extra[1] = 0;
            stl_normal n;
            stl_calculate_normal(n, &facet);
            stl_normalize_vector(n);
            facet.normal = n;
            stl.facet_start[triOff + i - 1] = facet;
        }
        nodeOff += tri->NbNodes();
        triOff += tri->NbTriangles();
    }

    TriangleMesh result;
    result.from_stl(stl);
    return result;
}

} // namespace Slic3r
