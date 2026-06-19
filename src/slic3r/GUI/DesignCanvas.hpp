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
    // Persistently draw committed sketches (un-consumed ones stay visible).
    void set_display_sketches(std::vector<DesignSketchTool::DisplaySketch> ds);
    void set_body_highlight(bool on);   // tint the solid when its feature is tree-selected
    void delete_selected_sketch_entities();
    void clear_sketch_selection();

    // Dimension tool: act on the current sketch selection.
    DesignSketchTool::DimType sketch_dimension_kind() const;
    double sketch_dimension_current() const;
    void   apply_sketch_dimension(double v);

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

    DesignSketchTool m_sketch_tool;
    std::unique_ptr<SketchInlineEditor> m_inline_editor;  // floating in-canvas value editor
    std::function<void(const SketchProfile&, const SketchPlane&)> m_on_sketch_commit;
    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> m_on_sketch_entities_commit;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignCanvas_hpp_
