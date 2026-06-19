#ifndef slic3r_CadDocument_hpp_
#define slic3r_CadDocument_hpp_

#include "TriangleMesh.hpp"
#include "SketchEngine.hpp"
#include "GeometryEngine.hpp"   // FaceGroup

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <string>
#include <vector>

namespace Slic3r {

enum class CadFeatureType { Sketch, Extrude, Fillet, Chamfer, Hole, Thread };
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

    // Real 2D sketch geometry (Onshape-style). When non-empty this takes
    // precedence over the shape/width/height/radius enum path in build_sketch_wire.
    SketchProfile profile;

    // Onshape-style multi-entity sketch geometry. When non-empty this takes
    // precedence over both `profile` and the shape-enum path in build_sketch_wire.
    std::vector<SketchEntity> entities;

    // 2D geometric constraints on `profile` (point indices). Solved in place.
    std::vector<SketchConstraintDef> constraints;

    // Onshape-style constraints on `entities` (Fase 4.2). Solved in place against
    // entity endpoints. Used when `entities` is non-empty (the legacy `constraints`
    // vector applies only to the `profile` path).
    std::vector<SketchEntityConstraintDef> entity_constraints;

    // Imported rigid 2D art (Text glyphs / SVG vector paths) as filled regions.
    // Each region: contour[0] = outer loop, contour[1..] = holes; points in
    // plane (u,v) millimetres. Rendered as a sketch overlay and extruded via a
    // faces-with-holes path (SketchEngine::make_extrude_regions) — deliberately
    // NOT solver entities, so imported art contributes zero DoF and never
    // pollutes the constraint solver / DoF readout. When non-empty it takes
    // precedence over the entities/profile/shape paths in the Extrude case.
    std::vector<std::vector<std::vector<Vec2d>>> imported_regions;

    // Non-destructive placement transform for imported_regions (Text/SVG),
    // applied at display + extrude time as
    //   p -> (p.x*import_scale_x + import_offset.x, p.y*import_scale_y + import_offset.y).
    // Lets the art be moved / enlarged / stretched (independent X/Y) repeatedly
    // without re-vectorising. Identity = no change.
    Vec2d  import_offset{0, 0};
    double import_scale_x{1.0};
    double import_scale_y{1.0};

    // Extrude params
    int         sketch_ref{-1};   // index into features[] of the consumed sketch
    double      distance{10};
    bool        symmetric{false};
    BooleanMode mode{BooleanMode::New};

    // Dress-up params (Fillet/Chamfer) — applied to the current body in order
    double      dressup_size{1.0};         // fillet radius or chamfer distance
    FaceGroup   face_group{FaceGroup::All};

    // Hole params (positioned circular cut into the current body)
    double      hole_diameter{5};
    double      hole_depth{10};
    bool        hole_through{true};        // true = symmetric through-cut, ignores hole_depth
    double      hole_x{0};                 // position on the plane (plane u/x axis)
    double      hole_y{0};                 // position on the plane (plane v/y axis)

    // Thread params (helical thread about the plane normal at a positioned point)
    double      thread_radius{5};          // nominal cylinder radius
    double      thread_pitch{2};           // axial advance per turn
    double      thread_height{10};         // total axial length
    double      thread_depth{1};           // radial crest depth of the thread profile
    bool        thread_internal{false};    // false = external threaded rod (New body);
                                           // true = tapped bore cut into the current body
    double      thread_x{0};               // axis position on the plane (u/x axis)
    double      thread_y{0};               // axis position on the plane (v/y axis)
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
    int  add_sketch_profile(const SketchProfile& profile, const SketchPlane& plane,
                            const std::string& name);
    // Onshape-style multi-entity sketch: stores the entity list verbatim. When
    // non-empty it takes precedence over profile/enum in build_sketch_wire.
    int  add_sketch_entities(const std::vector<SketchEntity>& entities,
                             const SketchPlane& plane, const std::string& name,
                             const std::vector<SketchEntityConstraintDef>& constraints = {});
    // Solve features[index]'s sketch constraints, writing solved coordinates back
    // into its profile.points. No-op (returns true) if the feature has no
    // constraints. Returns false if index is invalid / not a Sketch / solve fails.
    bool solve_sketch_feature(int index);
    int  add_extrude(int sketch_ref, double distance, bool symmetric,
                     BooleanMode mode, const std::string& name);
    int  add_fillet(double radius, FaceGroup faces, const std::string& name);
    int  add_chamfer(double distance, FaceGroup faces, const std::string& name);
    int  add_hole(double diameter, double depth, bool through,
                  double x, double y, const SketchPlane& plane,
                  const std::string& name);
    int  add_thread(double radius, double pitch, double height, double depth,
                    bool internal, double x, double y, const SketchPlane& plane,
                    const std::string& name);
    void clear();
    bool recompute();   // replay features -> body + display_mesh; false on error

    // Feature-tree editing (Onshape-style). All are transactional: they snapshot
    // features, mutate, recompute(), and roll back to the snapshot (re-recomputing)
    // if the result is invalid — so a failed edit never leaves a broken body.
    //
    // remove_feature: erase features[index]; deleting a Sketch cascades to the
    //   Extrude(s) that consume it; surviving sketch_ref indices are remapped.
    // move_feature:   shift features[index] by delta (-1 up / +1 down), clamped;
    //   sketch_ref indices of the two swapped slots are remapped.
    // replace_feature: overwrite features[index] with `edited` (its name and, for
    //   an Extrude, its sketch_ref are preserved from the original).
    bool remove_feature(int index);
    bool move_feature(int index, int delta);
    bool replace_feature(int index, const CadFeature& edited);
    // replace_sketch_extrude: a box is two linked features (Sketch + Extrude);
    //   overwrite both slots from one `edited` candidate (sketch params ->
    //   features[sketch_idx], extrude params -> features[extrude_idx]), keeping
    //   each slot's name/type and the sketch_ref link. Transactional like above.
    bool replace_sketch_extrude(int sketch_idx, int extrude_idx, const CadFeature& edited);

    // Apply ONE candidate feature on top of the current committed body and
    // tessellate the result into out_mesh, WITHOUT modifying features/body/
    // display_mesh. Returns false (with err set) if the candidate is invalid.
    // Used by the Design tab to show a translucent ghost before Confirm.
    bool preview(const CadFeature& candidate, TriangleMesh& out_mesh, std::string& err) const;

private:
    TopoDS_Wire build_sketch_wire(const CadFeature& sketch) const;
    // Apply a single feature to (result, have_body), throwing std::runtime_error on
    // failure. Shared by recompute() (replay) and preview() (single candidate).
    void apply_feature(TopoDS_Shape& result, bool& have_body, const CadFeature& f) const;
};

} // namespace Slic3r

#endif // slic3r_CadDocument_hpp_
