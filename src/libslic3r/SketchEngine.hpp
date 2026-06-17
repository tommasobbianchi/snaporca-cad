#ifndef slic3r_SketchEngine_hpp_
#define slic3r_SketchEngine_hpp_

#include "TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include "GeometryEngine.hpp"

#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <vector>

namespace Slic3r {

struct SketchSegment {
    enum Type { Line, Arc, Circle, Rectangle, Polygon };
    Type type{Line};
    Vec2d p0{0,0}, p1{0,0};
    Vec2d center{0,0};
    double radius{0}, start_angle{0}, end_angle{0};
    std::vector<Vec2d> points;
    template<class Archive>
    void serialize(Archive& ar) { ar(type, p0, p1, center, radius, start_angle, end_angle, points); }
};

struct SketchPlane {
    Vec3d origin{0,0,0};
    Vec3d normal{0,0,1};
    Vec3d x_axis{1,0,0};
    Vec3d y_axis{0,1,0};

    gp_Pln to_occt() const;
    static SketchPlane from_face(const TopoDS_Face& face);
    static SketchPlane XY() { return {}; }
    static SketchPlane XZ() { return {{0,0,0}, {0,1,0}, {1,0,0}, {0,0,1}}; }
    static SketchPlane YZ() { return {{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}}; }

    Vec2d project(const Vec3d& ray_origin, const Vec3d& ray_dir) const;
    Vec3d to_world(const Vec2d& pt) const;

    template<class Archive>
    void serialize(Archive& ar) { ar(origin, normal, x_axis, y_axis); }
};

struct SketchProfile {
    std::vector<Vec2d> points;
    bool closed{false};

    bool is_closed(double tolerance = 0.5) const;
    bool try_close(double tolerance = 0.5);
    void clear() { points.clear(); closed = false; }
    TopoDS_Wire to_occt_wire(const SketchPlane& plane) const;

    template<class Archive>
    void serialize(Archive& ar) { ar(points, closed); }
};

enum class SketchConstraintType {
    Fix, Coincident, Horizontal, Vertical, Distance,
    LockX, LockY, EqualLength, Parallel, Perpendicular
};

// Constraint on a SketchProfile, referencing profile point indices (a,b,c,d).
// `value` carries the target for Distance/LockX/LockY (ignored otherwise).
struct SketchConstraintDef {
    SketchConstraintType type{SketchConstraintType::Coincident};
    int    a{-1}, b{-1}, c{-1}, d{-1};
    double value{0.0};
    template<class Archive> void serialize(Archive& ar) { ar(type, a, b, c, d, value); }
};

struct SketchParams {
    // Extrude/Revolve
    double extrude_len{10}; bool extrude_sym{false}; double extrude_taper{0};
    double revolve_deg{360};
    bool   is_pocket{false}; // cut into selected object instead of new

    // Dress-up
    bool        dressup_enabled{false};
    DressUpType dressup_type{DressUpType::Fillet};
    FaceGroup   dressup_faces{FaceGroup::All};
    double      dressup_radius{1.0};
    double      dressup_chamfer_dist{1.0};

    // Mesh
    double linear_deflection{0.01};

    template<class Archive>
    void serialize(Archive& ar) {
        ar(extrude_len, extrude_sym, extrude_taper, revolve_deg, is_pocket,
           dressup_enabled, dressup_type, dressup_faces, dressup_radius, dressup_chamfer_dist,
           linear_deflection);
    }
};

class SketchEngine
{
public:
    static TopoDS_Shape make_extrude(const TopoDS_Wire& wire, const SketchPlane& plane,
                                     double length, bool symmetric = false, double taper_deg = 0.0);
    static TopoDS_Shape make_extrude_face(const TopoDS_Face& face, const SketchPlane& plane,
                                          double length, bool symmetric = false, double taper_deg = 0.0);

    static TopoDS_Shape make_revolve(const TopoDS_Wire& wire, const SketchPlane& plane,
                                     double angle_deg = 360.0);

    static TopoDS_Shape make_pocket(const TopoDS_Wire& wire, const SketchPlane& plane,
                                    const TopoDS_Shape& target, double depth);

    static TriangleMesh tessellate(const TopoDS_Shape& shape,
                                   double linear_deflection = 0.01,
                                   double angular_deflection = 0.5);
};

} // namespace Slic3r

#endif // slic3r_SketchEngine_hpp_
