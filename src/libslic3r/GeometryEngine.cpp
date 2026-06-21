#include "GeometryEngine.hpp"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <stdexcept>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp.hxx>
#include <TopTools.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomLProp_SLProps.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>

namespace Slic3r {

// ---- Primitive creation ----

TopoDS_Solid GeometryEngine::make_primitive(const PrimitiveParams& params)
{
    switch (params.type) {
    case PrimitiveType::Box:
        return BRepPrimAPI_MakeBox(gp_Pnt(-params.box_w/2, -params.box_d/2, 0),
                                   params.box_w, params.box_d, params.box_h).Solid();
    case PrimitiveType::Cylinder:
        return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0), gp_Dir(0,0,1)),
                                        params.cyl_radius, params.cyl_height).Solid();
    case PrimitiveType::Sphere:
        return BRepPrimAPI_MakeSphere(gp_Pnt(0,0,params.sph_radius), params.sph_radius).Solid();
    case PrimitiveType::Cone:
        return BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,0), gp_Dir(0,0,1)),
                                    params.cone_r1, params.cone_r2, params.cone_height).Solid();
    case PrimitiveType::Torus:
        return BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0,0,params.torus_r2), gp_Dir(0,0,1)),
                                     params.torus_r1, params.torus_r2).Solid();
    default:
        return BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,0), 20,20,20).Solid();
    }
}

// ---- Face classification ----

FaceGroup GeometryEngine::classify_face(const TopoDS_Face& face, const TopoDS_Shape& /*solid*/)
{
    try {
        BRepAdaptor_Surface surf(face);
        if (surf.GetType() == GeomAbs_Plane) {
            // Sample normal at center UV
            double u = (surf.FirstUParameter() + surf.LastUParameter()) / 2.0;
            double v = (surf.FirstVParameter() + surf.LastVParameter()) / 2.0;
            gp_Pnt pt; gp_Vec du, dv;
            surf.D1(u, v, pt, du, dv);
            gp_Dir n = du.Crossed(dv);
            if (face.Orientation() == TopAbs_REVERSED) n.Reverse();

            if (n.Z() > 0.7)  return FaceGroup::Top;
            if (n.Z() < -0.7) return FaceGroup::Bottom;
            return FaceGroup::Lateral;
        }
    } catch (...) {}
    return FaceGroup::Lateral;
}

// ---- Edge collection ----

std::vector<TopoDS_Edge> GeometryEngine::collect_edges(const TopoDS_Shape& solid, FaceGroup target)
{
    std::vector<TopoDS_Edge> result;
    if (target == FaceGroup::All) {
        for (TopExp_Explorer exp(solid, TopAbs_EDGE); exp.More(); exp.Next())
            result.push_back(TopoDS::Edge(exp.Current()));
        return result;
    }

    // Build edge-to-face map once
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(solid, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    for (TopExp_Explorer edgeExp(solid, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
        if (!edgeFaceMap.Contains(edge)) continue;
        const TopTools_ListOfShape& faces = edgeFaceMap.FindFromKey(edge);

        bool include = false;
        for (auto it = faces.begin(); it != faces.end(); ++it) {
            FaceGroup fg = classify_face(TopoDS::Face(*it), solid);
            if (target == FaceGroup::Top && fg == FaceGroup::Top)       { include = true; break; }
            if (target == FaceGroup::Bottom && fg == FaceGroup::Bottom) { include = true; break; }
            if (target == FaceGroup::Lateral && fg == FaceGroup::Lateral) { include = true; break; }
        }

        if (!include && target == FaceGroup::Top) {
            for (auto it = faces.begin(); it != faces.end(); ++it) {
                if (classify_face(TopoDS::Face(*it), solid) == FaceGroup::Top) { include = true; break; }
            }
        }
        if (!include && target == FaceGroup::Bottom) {
            for (auto it = faces.begin(); it != faces.end(); ++it) {
                if (classify_face(TopoDS::Face(*it), solid) == FaceGroup::Bottom) { include = true; break; }
            }
        }
        if (target == FaceGroup::Lateral && !include) {
            int lateralCount = 0;
            for (auto it = faces.begin(); it != faces.end(); ++it) {
                if (classify_face(TopoDS::Face(*it), solid) == FaceGroup::Lateral) ++lateralCount;
            }
            if (lateralCount >= 2) include = true;
        }

        if (include) result.push_back(edge);
    }

    return result;
}

// ---- Fillet/Chamfer ----

TopoDS_Shape GeometryEngine::apply_fillet(const TopoDS_Shape& solid, double radius, FaceGroup faces)
{
    if (radius <= 0.001) return solid;

    std::vector<TopoDS_Edge> edges = collect_edges(solid, faces);
    if (edges.empty()) return solid;

    BRepFilletAPI_MakeFillet fillet(solid);
    for (const auto& edge : edges)
        fillet.Add(radius, edge);
    fillet.Build();

    // A too-large radius (e.g. >= half the smallest spanned dimension) makes the
    // operation degenerate; OCCT leaves IsDone() false. Report it instead of
    // silently returning the unfilleted solid (which reads as a false success).
    if (!fillet.IsDone()) throw std::runtime_error("fillet radius too large for this geometry");
    return fillet.Shape();
}

TopoDS_Shape GeometryEngine::apply_chamfer(const TopoDS_Shape& solid, double distance, FaceGroup faces)
{
    if (distance <= 0.001) return solid;

    std::vector<TopoDS_Edge> edges = collect_edges(solid, faces);
    if (edges.empty()) return solid;

    BRepFilletAPI_MakeChamfer chamfer(solid);
    for (const auto& edge : edges)
        chamfer.Add(distance, edge); // symmetric chamfer
    chamfer.Build();

    if (!chamfer.IsDone()) throw std::runtime_error("chamfer distance too large for this geometry");
    return chamfer.Shape();
}

TopoDS_Shape GeometryEngine::apply_fillet(const TopoDS_Shape& solid, double radius, int edge_id)
{
    if (radius <= 0.001) return solid;

    TopoDS_Edge edge = edge_by_index(solid, edge_id);
    if (edge.IsNull()) throw std::runtime_error("apply_fillet: invalid edge id");

    BRepFilletAPI_MakeFillet mk(solid);
    mk.Add(radius, edge);
    mk.Build();

    if (!mk.IsDone()) throw std::runtime_error("apply_fillet: OCCT fillet failed");
    return mk.Shape();
}

TopoDS_Shape GeometryEngine::apply_chamfer(const TopoDS_Shape& solid, double distance, int edge_id)
{
    if (distance <= 0.001) return solid;

    TopoDS_Edge edge = edge_by_index(solid, edge_id);
    if (edge.IsNull()) throw std::runtime_error("apply_chamfer: invalid edge id");

    BRepFilletAPI_MakeChamfer mk(solid);
    mk.Add(distance, edge);
    mk.Build();

    if (!mk.IsDone()) throw std::runtime_error("apply_chamfer: OCCT chamfer failed");
    return mk.Shape();
}

// ---- Tessellation ----

TriangleMesh GeometryEngine::tessellate(const TopoDS_Shape& shape,
                                        double linear_deflection,
                                        double angular_deflection)
{
    BRepMesh_IncrementalMesh mesh(shape, linear_deflection, false, angular_deflection, true);

    int nbNodes = 0, nbTri = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(exp.Current()), loc);
        if (!tri.IsNull()) { nbNodes += tri->NbNodes(); nbTri += tri->NbTriangles(); }
    }
    if (nbTri == 0 || nbNodes == 0) return TriangleMesh{};

    stl_file stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = (uint32_t)nbTri;
    stl.stats.original_num_facets = stl.stats.number_of_facets;
    stl_allocate(&stl);

    std::vector<Vec3f> pts; pts.reserve(nbNodes);
    int ndOff = 0, trOff = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Shape& F = exp.Current();
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(F), loc);
        if (tri.IsNull()) continue;
        gp_Trsf T = loc.Transformation();
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            gp_Pnt p = tri->Node(i); p.Transform(T);
            pts.emplace_back(Vec3f(p.X(), p.Y(), p.Z()));
        }
        auto orient = exp.Current().Orientation();
        int ids[3];
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            Poly_Triangle t = tri->Triangle(i); t.Get(ids[0], ids[1], ids[2]);
            if (orient == TopAbs_REVERSED) std::swap(ids[1], ids[2]);
            stl_facet f;
            f.vertex[0] = pts[ids[0]+ndOff-1].cast<float>();
            f.vertex[1] = pts[ids[1]+ndOff-1].cast<float>();
            f.vertex[2] = pts[ids[2]+ndOff-1].cast<float>();
            f.extra[0]=0; f.extra[1]=0;
            stl_normal n; stl_calculate_normal(n,&f); stl_normalize_vector(n);
            f.normal=n; stl.facet_start[trOff+i-1]=f;
        }
        ndOff += tri->NbNodes(); trOff += tri->NbTriangles();
    }
    TriangleMesh result; result.from_stl(stl); return result;
}

std::string GeometryEngine::primitive_name(PrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Box:      return "Box";
    case PrimitiveType::Cylinder: return "Cylinder";
    case PrimitiveType::Sphere:   return "Sphere";
    case PrimitiveType::Cone:     return "Cone";
    case PrimitiveType::Torus:    return "Torus";
    default:                      return "Unknown";
    }
}

// ---- Topology accessors ----

int GeometryEngine::face_count(const TopoDS_Shape& shape)
{
    int n = 0;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next())
        ++n;
    return n;
}

TopoDS_Face GeometryEngine::face_by_index(const TopoDS_Shape& shape, int index)
{
    if (index < 0) return TopoDS_Face();
    int ordinal = 0;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
        if (ordinal == index)
            return TopoDS::Face(e.Current());
        ++ordinal;
    }
    return TopoDS_Face();
}

std::vector<TopoDS_Edge> GeometryEngine::edges_of_face(const TopoDS_Face& face)
{
    std::vector<TopoDS_Edge> result;
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(face, TopAbs_EDGE, map);
    for (int i = 1; i <= map.Extent(); ++i)
        result.push_back(TopoDS::Edge(map(i)));
    return result;
}

std::vector<Vec3d> GeometryEngine::sample_edge_world(const TopoDS_Edge& edge, double chord_tol)
{
    if (BRep_Tool::Degenerated(edge))
        return {};

    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection disc(curve, 0.1, chord_tol);

    std::vector<Vec3d> pts;
    if (disc.NbPoints() >= 2) {
        for (int i = 1; i <= disc.NbPoints(); ++i) {
            gp_Pnt p = disc.Value(i);
            pts.emplace_back(p.X(), p.Y(), p.Z());
        }
    } else {
        gp_Pnt p0 = curve.Value(curve.FirstParameter());
        gp_Pnt p1 = curve.Value(curve.LastParameter());
        pts.emplace_back(p0.X(), p0.Y(), p0.Z());
        pts.emplace_back(p1.X(), p1.Y(), p1.Z());
    }
    return pts;
}

Vec3d GeometryEngine::face_centroid_world(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    gp_Pnt c = props.CentreOfMass();
    return Vec3d(c.X(), c.Y(), c.Z());
}

Vec3d GeometryEngine::face_normal_world(const TopoDS_Face& face)
{
    BRepAdaptor_Surface surf(face);
    const double u = 0.5 * (surf.FirstUParameter() + surf.LastUParameter());
    const double v = 0.5 * (surf.FirstVParameter() + surf.LastVParameter());
    BRepLProp_SLProps props(surf, u, v, 1, 1e-6);
    gp_Dir n(0.0, 0.0, 1.0);
    if (props.IsNormalDefined()) n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();   // outward (account for face winding)
    return Vec3d(n.X(), n.Y(), n.Z());
}

int GeometryEngine::edge_count(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    return map.Extent();
}

TopoDS_Edge GeometryEngine::edge_by_index(const TopoDS_Shape& shape, int index)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    if (index < 0 || index >= map.Extent())
        return TopoDS_Edge();
    return TopoDS::Edge(map(index + 1));
}

int GeometryEngine::edge_index_of(const TopoDS_Shape& shape, const TopoDS_Edge& edge)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    int idx = map.FindIndex(edge);
    return (idx > 0) ? (idx - 1) : -1;
}

} // namespace Slic3r
