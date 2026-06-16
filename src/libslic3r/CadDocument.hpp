#ifndef slic3r_CadDocument_hpp_
#define slic3r_CadDocument_hpp_

#include "TriangleMesh.hpp"
#include "SketchEngine.hpp"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <string>
#include <vector>

namespace Slic3r {

enum class CadFeatureType { Sketch, Extrude };
enum class SketchShape    { Rectangle, Circle };
enum class BooleanMode    { New, Add, Cut };

struct CadFeature {
    CadFeatureType type{CadFeatureType::Sketch};
    std::string    name;
    bool           enabled{true};

    // Sketch params (centered on the plane origin)
    SketchShape shape{SketchShape::Rectangle};
    SketchPlane plane{SketchPlane::XY()};
    double      width{20};
    double      height{20};
    double      radius{10};

    // Extrude params
    int         sketch_ref{-1};   // index into features[] of the consumed sketch
    double      distance{10};
    bool        symmetric{false};
    BooleanMode mode{BooleanMode::New};
};

// OCCT-only feature tree backing the Design tab. No GUI dependencies (lives in libslic3r).
class CadDocument {
public:
    std::vector<CadFeature> features;
    TopoDS_Shape            body;          // current solid after replay
    TriangleMesh            display_mesh;  // tessellation of body
    std::string             error;         // last recompute error ("" = ok)

    double linear_deflection{0.01};
    double angular_deflection{0.5};

    int  add_sketch(SketchShape shape, const SketchPlane& plane,
                    double width, double height, double radius,
                    const std::string& name);
    int  add_extrude(int sketch_ref, double distance, bool symmetric,
                     BooleanMode mode, const std::string& name);
    void clear();
    bool recompute();   // replay features -> body + display_mesh; false on error

private:
    TopoDS_Wire build_sketch_wire(const CadFeature& sketch) const;
};

} // namespace Slic3r

#endif // slic3r_CadDocument_hpp_
