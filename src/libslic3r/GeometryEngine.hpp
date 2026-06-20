#ifndef slic3r_GeometryEngine_hpp_
#define slic3r_GeometryEngine_hpp_

#include "TriangleMesh.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <gp_Ax2.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <vector>

namespace Slic3r {

enum class PrimitiveType { Box, Cylinder, Sphere, Cone, Torus, COUNT };
enum class DressUpType   { Fillet, Chamfer };
enum class FaceGroup     { Top, Bottom, Lateral, All };

struct PrimitiveParams {
    PrimitiveType type{PrimitiveType::Box};
    double box_w{20}, box_h{20}, box_d{20};
    double cyl_radius{10}, cyl_height{20};
    double sph_radius{10};
    double cone_r1{10}, cone_r2{5}, cone_height{20};
    double torus_r1{10}, torus_r2{3};

    // Dress-up
    bool   dressup_enabled{false};
    DressUpType dressup_type{DressUpType::Fillet};
    FaceGroup   dressup_faces{FaceGroup::All};
    double dressup_radius{1.0};    // fillet radius
    double dressup_chamfer_dist{1.0}; // chamfer distance (symmetric)

    // Mesh quality
    double linear_deflection{0.01};
    double angular_deflection{0.5};

    template<class Archive>
    void serialize(Archive& ar) {
        ar(type, box_w, box_h, box_d, cyl_radius, cyl_height, sph_radius,
           cone_r1, cone_r2, cone_height, torus_r1, torus_r2,
           dressup_enabled, dressup_type, dressup_faces, dressup_radius, dressup_chamfer_dist,
           linear_deflection, angular_deflection);
    }
};

class GeometryEngine
{
public:
    static TopoDS_Solid make_primitive(const PrimitiveParams& params);

    static TopoDS_Shape apply_fillet(const TopoDS_Shape& solid, double radius,
                                     FaceGroup faces = FaceGroup::All);
    static TopoDS_Shape apply_fillet(const TopoDS_Shape& solid, double radius,
                                     int edge_id);
    static TopoDS_Shape apply_chamfer(const TopoDS_Shape& solid, double distance,
                                       FaceGroup faces = FaceGroup::All);
    static TopoDS_Shape apply_chamfer(const TopoDS_Shape& solid, double distance,
                                       int edge_id);

    static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                   double linear_deflection = 0.01,
                                   double angular_deflection = 0.5);
    static std::string  primitive_name(PrimitiveType type);

    // Topology accessors for in-viewport face/edge picking (Design tab). Face index is the
    // TopExp_Explorer(shape, TopAbs_FACE) ordinal — identical to SketchEngine::tessellate's
    // per-triangle face id, so a picked triangle's id maps back to a face here.
    static TopoDS_Face face_by_index(const TopoDS_Shape& shape, int index);  // null if out of range
    static int         face_count(const TopoDS_Shape& shape);
    static std::vector<TopoDS_Edge> edges_of_face(const TopoDS_Face& face);
    // Centre of mass (world) of a face — used to compute the extrude length for "up to face".
    static Vec3d face_centroid_world(const TopoDS_Face& face);
    // Sample an edge into a world-space polyline (>=2 pts) for pick-distance + highlight.
    static std::vector<Vec3d> sample_edge_world(const TopoDS_Edge& edge, double chord_tol = 0.05);
    // 0-based edge index into TopExp::MapShapes(shape, TopAbs_EDGE, map).
    static int          edge_count(const TopoDS_Shape& shape);
    static TopoDS_Edge  edge_by_index(const TopoDS_Shape& shape, int index);
    static int          edge_index_of(const TopoDS_Shape& shape, const TopoDS_Edge& edge);

private:
    static std::vector<TopoDS_Edge> collect_edges(const TopoDS_Shape& solid, FaceGroup faces);
    static FaceGroup classify_face(const TopoDS_Face& face, const TopoDS_Shape& solid);
};

} // namespace Slic3r

#endif // slic3r_GeometryEngine_hpp_
