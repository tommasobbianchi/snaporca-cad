#ifndef slic3r_DesignCanvas_hpp_
#define slic3r_DesignCanvas_hpp_

#include <wx/panel.h>

#include <functional>
#include <memory>
#include <string>

#include "3DBed.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SketchEngine.hpp"
#include "DesignSketchTool.hpp"

class wxGLCanvas;

namespace Slic3r {

class TriangleMesh;

namespace GUI {

class GLCanvas3D;
class SketchInlineEditor;

class DesignCanvas : public wxPanel
{
public:
    explicit DesignCanvas(wxWindow* parent);
    ~DesignCanvas() override;

    void set_mesh(const TriangleMesh& mesh);
    // Multi-body display: one GLVolume per body, each coloured distinctly (per-body colour).
    // `visible` (optional, indexed by body) hides bodies whose flag is false.
    void set_bodies(const std::vector<TriangleMesh>& body_meshes,
                    const std::vector<bool>& visible = {});
    void clear_mesh();

    void set_preview_mesh(const TriangleMesh& mesh);
    void clear_preview();

    void fit_view();
    void set_view(const std::string& view_name);

    void begin_sketch(const SketchPlane& plane, DesignSketchTool::Mode mode);
    // Re-open a committed entity sketch for full in-canvas editing (load geometry +
    // constraints, re-detect feature groups). Re-commits via finish_sketch().
    void edit_sketch(const std::vector<SketchEntity>& entities,
                     const std::vector<SketchEntityConstraintDef>& constraints,
                     const SketchPlane& plane);
    void set_sketch_tool(DesignSketchTool::Mode mode);
    void set_sketch_construction(bool c);
    void set_sketch_polygon_sides(int n);
    void set_sketch_polygon_circumscribed(bool c);
    void finish_sketch();
    bool is_sketching() const;
    void cancel_sketch();
    void set_on_sketch_commit(std::function<void(const SketchProfile&, const SketchPlane&)> cb);
    void set_on_sketch_entities_commit(
        std::function<void(const std::vector<SketchEntity>&,
                           const std::vector<SketchEntityConstraintDef>&,
                           const SketchPlane&)> cb);

    // Line tool: pending-segment length entry + live readout (Phase 2).
    void set_on_segment_drawn(std::function<void(double, double)> cb);
    void set_on_cursor_metrics(std::function<void(double, double, bool)> cb);
    void set_on_solve_state(std::function<void(int, bool, bool)> cb);  // dof, ok, has_constraints
    void apply_segment_length(double len);  // exact length, then commit & repaint
    void keep_segment_as_drawn();           // commit as-drawn & repaint

    // Sketch selection (Mode::Select).
    void set_on_sketch_selection_changed(std::function<void(int)> cb);
    void set_on_sketch_face_selected(std::function<void()> cb);  // closed loop clicked
    void set_on_display_sketch_selected(std::function<void(int, int)> cb);  // committed loop clicked: (feature, region)
    std::vector<SketchEntity> selected_loop_entities() const;  // entities of the click-selected loop
    std::vector<std::vector<int>> region_entity_indices(const std::vector<SketchEntity>& ents) const;
    void clear_loop_pick();  // drop the click-selected loop highlight (e.g. after extrude)
    // Solid whole/face/edge selection: point the tool at the bodies + concatenated
    // tessellation (with per-triangle face & body ids), and a callback fired on each
    // whole->face->edge cycle (level, body index, face id, edge id).
    void set_solid_pick(const std::vector<CadBody>* bodies, const TriangleMesh* mesh,
                        const std::vector<int>* tri_face, const std::vector<int>* tri_body,
                        const std::vector<bool>* visible = nullptr,
                        const std::vector<Transform3d>* xform = nullptr);
    void set_on_solid_selection_changed(std::function<void(int, int, int, int)> cb);
    void select_body(int body);   // Parts-list -> highlight a whole body by index
    // Move-body gizmo (M5): three world-axis drag arrows on a body; drag fires the move
    // callback with the body index + accumulated translation (display-only, host applies it).
    void begin_move_body(int body, const Vec3d& base, const Vec3d& offset);
    void clear_move_gizmo();
    bool moving_body() const;
    void set_on_body_move_changed(std::function<void(int, Vec3d)> cb);
    // Visual Fillet/Chamfer radius gizmo: when a solid edge is picked, anchor a radius arrow on
    // it; drag/edit fire the radius callback. Returns false if no edge is currently picked.
    bool begin_fillet_gizmo(const Vec3d& body_centroid, double radius);
    void clear_fillet_gizmo();
    bool filleting() const;
    void set_on_fillet_radius_changed(std::function<void(double)> cb);
    // Visual Hole gizmo: the panel feeds the hole plane + position + diameter/depth/through while
    // its Hole card is open; drag/edit fire the hole callback (x, y, diameter, depth).
    void begin_hole_gizmo(const SketchPlane& plane, double x, double y,
                          double diameter, double depth, bool through);
    void clear_hole_gizmo();
    bool holing() const;
    void set_on_hole_changed(std::function<void(double, double, double, double)> cb);
    // Visual Extrude depth-arrow gizmo (C5b): the panel feeds the profile plane + centroid +
    // live depths/flags while its Extrude card is open; drag/edit fire the depth callback.
    void set_extrude_gizmo(const SketchPlane& plane, const Vec2d& centroid,
                           double depth, double depth2, bool two_sided, bool flip);
    void clear_extrude_gizmo();
    void set_on_extrude_depth_changed(std::function<void(double, bool)> cb);
    void set_on_sketch_exit(std::function<void()> cb);           // Esc -> exit the tool
    // Persistently draw committed sketches (un-consumed ones stay visible).
    void set_display_sketches(std::vector<DesignSketchTool::DisplaySketch> ds);
    void set_body_highlight(bool on);   // tint the solid when its feature is tree-selected
    void delete_selected_sketch_entities();
    void clear_sketch_selection();

    // Dimension tool: act on the current sketch selection.
    DesignSketchTool::DimType sketch_dimension_kind() const;
    double sketch_dimension_current() const;
    void   apply_sketch_dimension(double v);

    // Open the in-canvas value editor at the cursor for a host-driven value (the
    // committed-feature Constrain path uses this instead of a docked numeric card).
    void   open_inline_value(double current, std::function<void(double)> commit,
                             std::function<void()> cancel = {});

    // Dimension tool (Mode::Dimension): click-to-place quotes. The pick-complete
    // callback lets the panel pop the value card; set/cancel apply or keep the value.
    void set_on_dimension_pick_complete(std::function<void(double)> cb);
    DesignSketchTool::DimType pending_dimension_type() const;
    void set_sketch_dimension_value(double v);
    void cancel_sketch_dimension();

    // Constrain mode: load a committed profile for picking + constraint editing.
    void begin_constrain(const SketchProfile& prof, const SketchPlane& plane);
    // Leave constrain mode and clear any picked-entity highlight from the overlay.
    void end_constrain();
    bool is_constraining() const;
    bool selected_segment(int& a, int& b) const;
    void update_constrain_profile(const std::vector<Vec2d>& pts);

    // Entity-aware Constrain (Fase 4.2): pick Line entities of a committed sketch.
    void begin_constrain_entities(const std::vector<SketchEntity>& ents, const SketchPlane& plane);
    bool is_constraining_entities() const;

    // In-canvas bbox transform of imported Text/SVG art (replaces the Move/Scale dialog).
    void begin_imported_transform(int feat,
                                  const std::vector<std::vector<std::vector<Vec2d>>>& base_regions,
                                  const SketchPlane& plane, const Vec2d& offset,
                                  double scale_x, double scale_y);
    void set_on_imported_transform(std::function<void(int, Vec2d, double, double)> cb);
    bool selected_constrain_entities(int& e0, int& e1) const;
    int  selected_constrain_axis() const;  // third pick slot (Symmetric axis), -1 if unset
    bool pick0_point(Vec2d& out) const;   // plane-coords of the slot-0 pick (trim/extend)
    void update_constrain_entities(const std::vector<SketchEntity>& ents);
    // Constraint manager (C3.4): highlight the entities referenced by a selected
    // constraint (yellow tint in Constrain mode); empty clears the highlight.
    void set_constraint_highlight(std::vector<int> entities);
    // Constraint glyph badges (C3.4b): the feature's constraints, drawn as iconic
    // marks near their entities in Constrain mode; empty clears them.
    void set_constraint_glyphs(std::vector<SketchEntityConstraintDef> cons);

private:
    void reload(bool keep_view);

    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};
    Bed3D       m_bed;
    Model       m_model;
    bool        m_first_frame{true};
    bool        m_body_selected{false};   // tree selected a body feature → tint the solid
    std::vector<bool> m_body_visible;     // per-body visibility (empty => all visible)

    DesignSketchTool m_sketch_tool;
    std::unique_ptr<SketchInlineEditor> m_inline_editor;  // floating in-canvas value editor
    std::function<void(const SketchProfile&, const SketchPlane&)> m_on_sketch_commit;
    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> m_on_sketch_entities_commit;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignCanvas_hpp_
