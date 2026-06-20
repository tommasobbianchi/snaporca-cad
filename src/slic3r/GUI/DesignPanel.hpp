#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/treebase.h>   // wxTreeItemId

#include <vector>
#include <functional>

#include "libslic3r/CadDocument.hpp"

class wxChoice;
class wxCheckBox;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxTreeCtrl;
class wxImageList;
class wxStaticText;
class wxSizer;
class wxButton;
class wxPanel;
class ScalableButton;

namespace Slic3r { namespace GUI {

class DesignCanvas;

// Design (CAD) tab: a sketch-first, Onshape-style form-driven CAD panel.
// Sketch and Extrude are independent tools: the user creates a Sketch first,
// then selects it and Extrudes to produce a solid.
class DesignPanel : public wxPanel
{
public:
    explicit DesignPanel(wxWindow* parent);

private:
    enum class Tool { None, Sketch, Extrude, Dressup, Hole, Thread };

    // Onshape-style contextual top toolbar: only the active mode's tool group is
    // shown (Feature = sketch/extrude/dress/hole/thread; Sketch = entity tools;
    // Constrain = constraints + edit ops). Replaces the old always-visible wall.
    enum class UiMode { Feature, Sketch, Constrain };
    void set_ui_mode(UiMode m);

    void on_shape_changed();
    void on_add_sketch();
    void on_add_extrude();
    void on_add_dressup();
    void on_add_hole();
    void on_add_thread();
    // Import rigid 2D art (Text / SVG) as a new Sketch feature carrying
    // imported_regions (no solver entities). on_add_text/on_import_svg gather
    // input; add_imported_sketch builds the feature, refreshes tree + display.
    void on_add_text();
    void on_import_svg();
    void add_imported_sketch(const std::vector<std::vector<std::vector<Vec2d>>>& regions,
                             const wxString& base_name);
    // Move / enlarge / stretch (independent X/Y) an imported Text/SVG sketch:
    // a modal dialog editing the feature's placement transform in place.
    void on_transform_imported(int feat_idx);
    void on_commit();
    void refresh_tree();
    void set_status_ok();

    // Feature-tree editing (Onshape-style): act on the selected tree row.
    void on_delete_feature();
    void on_move_feature(int delta);   // -1 = up, +1 = down
    void on_toggle_visibility();       // show/hide the selected feature (CadFeature::enabled)

    // Constrain mode: enter on the tree-selected sketch, then apply a geometric
    // constraint to the in-canvas picked segment and re-solve in the kernel.
    void on_begin_constrain();
    void apply_constraint(SketchConstraintType type);
    void apply_entity_constraint(SketchConstraintType type);  // Fase 4.2 entity path
    enum class EditOp { Mirror, Offset, Fillet, Trim, Extend, Array, Move, Chamfer, Rotate, Scale, PolarArray }; // Fase 4.4/4.5/4.6 sketch edit ops
    void apply_edit_op(EditOp op);                            // mutate selected sketch entities
    // Onshape-style docked value entry (replaces wxGetTextFromUser popups for
    // Angle/Radius/Diameter constraints + Offset/Fillet edit ops). request_value
    // shows the card and stows a continuation run by confirm_value().
    void request_value(const wxString& label, double def, double mn, double mx,
                       std::function<void(double)> cont,
                       std::function<void()> on_cancel = nullptr);
    void confirm_value();
    void cancel_value();
    void commit_entity_constraint(const SketchEntityConstraintDef& def); // shared solve/refresh tail
    void commit_entity_constraints(const std::vector<SketchEntityConstraintDef>& defs); // multi-def (Symmetric)

    // Constraint manager (C3.4): a docked list of the constrained sketch's
    // entity-constraints with per-row select (highlight the referenced entities in
    // the viewport) and delete (drop the constraint + re-solve). Shown in Constrain
    // mode only; operates on m_doc.features[m_constrain_feat].entity_constraints.
    void rebuild_constraint_list();                                      // refill m_constraint_rows
    void delete_constraint(int idx);                                     // erase + re-solve + refresh
    void highlight_constraint_entities(int idx);                         // push referenced entities to viewport
    void refresh_constrain_dof();                                        // re-solve feature, mirror DoF readout
    wxString constraint_label(const SketchEntityConstraintDef& d) const; // human-readable row text
    void after_edit_op();                                                // shared edit-op refresh tail
    void on_edit_feature();            // reopen the selected feature's dialog populated
    void after_tree_edit(bool ok);     // shared post-op refresh of tree/viewport/status
    void load_feature_into_dialog(const CadFeature& f);
    void reset_edit_state();           // back to add-mode (m_edit_index = -1)

    // Onshape loop: Button -> open_tool (show dialog) -> refresh_preview (ghost) ->
    // confirm_tool (commit) / cancel_tool (abort).
    void       open_tool(Tool t);
    void       close_tool();
    void       refresh_preview();
    void       confirm_tool();
    void       cancel_tool();
    CadFeature build_candidate(Tool t) const;
    int        resolve_extrude_sketch() const;
    // True when Extrude should build only the click-selected loop (a region of the
    // resolved sketch is selected and it carries entities).
    bool       extrude_uses_loop() const;
    void       sync_sketch_display();   // push un-consumed committed sketches to the viewport
    // Feed the viewport's visual Extrude depth-arrow gizmo (C5b) with the current profile
    // plane + centroid + live depths while the Extrude card is open (self-gates on m_active).
    void       update_extrude_gizmo();

    CadDocument m_doc;

    Tool      m_active{Tool::None};
    wxSizer*  m_box_sketch{nullptr};
    wxSizer*  m_box_extrude{nullptr};
    wxSizer*  m_box_dressup{nullptr};
    wxSizer*  m_box_hole{nullptr};
    wxSizer*  m_box_thread{nullptr};

    // Onshape-style dialog-card title rows (icon + bold feature name), retitled
    // per tool in open_tool() (edit-mode shows the feature's actual name).
    wxStaticText* m_hdr_sketch{nullptr};
    // Onshape sketch-entry card (plane/orientation) that opens on "New sketch" and
    // persists until Finish (Phase 3).
    wxSizer*      m_box_sketch_session{nullptr};
    wxStaticText* m_hdr_sketch_session{nullptr};
    wxStaticText* m_hdr_extrude{nullptr};
    wxStaticText* m_hdr_dressup{nullptr};
    wxStaticText* m_hdr_hole{nullptr};
    wxStaticText* m_hdr_thread{nullptr};

    wxScrolledWindow* m_form{nullptr};
    DesignCanvas*     m_viewport{nullptr};

    // Top contextual toolbar (parented to the panel, above the form/viewport row).
    UiMode    m_ui_mode{UiMode::Feature};
    wxPanel*  m_toolbar{nullptr};
    wxSizer*  m_tb_feature{nullptr};
    wxSizer*  m_tb_sketch{nullptr};
    wxSizer*  m_tb_constrain{nullptr};
    wxCheckBox*       m_construction{nullptr};   // sketch-mode construction toggle
    wxSpinCtrl*       m_sides{nullptr};          // polygon sides
    wxCheckBox*       m_poly_circ{nullptr};      // polygon circumscribed toggle

    wxChoice*         m_draw_plane{nullptr};
    wxChoice*         m_shape{nullptr};
    wxChoice*         m_plane{nullptr};
    wxChoice*         m_mode{nullptr};
    wxSpinCtrlDouble* m_width{nullptr};
    wxSpinCtrlDouble* m_height{nullptr};
    wxSpinCtrlDouble* m_radius{nullptr};
    wxSpinCtrlDouble* m_distance{nullptr};
    wxChoice*         m_extrude_end{nullptr};   // Blind/Symmetric/TwoSided/ThroughAll/UpTo*
    wxSpinCtrlDouble* m_distance2{nullptr};     // second-side depth (Two-sided)
    wxSpinCtrlDouble* m_taper{nullptr};         // draft angle (deg)
    wxCheckBox*       m_flip{nullptr};          // reverse extrude direction

    wxStaticText*     m_extrude_sketch_label{nullptr};
    int               m_extrude_sketch_ref{-1};
    // Plate loop selection (click a committed sketch loop): the Sketch feature + the
    // clicked closed-region index, so Extrude builds just that one loop. -1 = none.
    int               m_sel_sketch_feat{-1};
    int               m_sel_sketch_region{-1};
    // Click-selected solid topology (whole/face/edge cycle): face id for up-to-face / dress-up.
    int               m_sel_solid_face{-1};
    int               m_sel_solid_edge{-1};
    // Face-as-profile extrude (Onshape): when Extrude is opened on a picked solid face with
    // no sketch source, this carries that global face id so the kernel extrudes the face.
    // -1 = ordinary sketch/loop extrude. Set when opening the Extrude card, consumed on add.
    int               m_extrude_face_src{-1};

    wxChoice*         m_dressup_type{nullptr};
    wxChoice*         m_face_group{nullptr};
    wxSpinCtrlDouble* m_dressup_size{nullptr};

    wxChoice*         m_hole_plane{nullptr};
    wxSpinCtrlDouble* m_hole_diameter{nullptr};
    wxSpinCtrlDouble* m_hole_depth{nullptr};
    wxCheckBox*       m_hole_through{nullptr};
    wxSpinCtrlDouble* m_hole_x{nullptr};
    wxSpinCtrlDouble* m_hole_y{nullptr};

    wxChoice*         m_thread_plane{nullptr};
    wxSpinCtrlDouble* m_thread_radius{nullptr};
    wxSpinCtrlDouble* m_thread_pitch{nullptr};
    wxSpinCtrlDouble* m_thread_height{nullptr};
    wxSpinCtrlDouble* m_thread_depth{nullptr};
    wxCheckBox*       m_thread_internal{nullptr};
    wxSpinCtrlDouble* m_thread_x{nullptr};
    wxSpinCtrlDouble* m_thread_y{nullptr};

    // Onshape-style docked value-entry card (Angle/Radius/Diameter/Offset/Fillet).
    wxSizer*          m_box_value{nullptr};
    wxStaticText*     m_value_label{nullptr};
    wxTextCtrl*       m_value_input{nullptr};   // plain text field: forces en ('.') decimals
    double            m_value_min{0.0};         // range for confirm-time clamping
    double            m_value_max{0.0};
    std::function<void(double)> m_value_cont;   // deferred apply, run on Confirm
    std::function<void()>       m_value_cancel; // optional action when the card is cancelled

    // Feature tree: a wxTreeCtrl with per-feature-type icons. Callers keep using
    // integer row indices via tree_selection()/set_tree_selection(); m_tree_items
    // maps feature order -> tree node, rebuilt by refresh_tree().
    wxTreeCtrl*               m_tree{nullptr};
    wxImageList*              m_tree_images{nullptr};
    std::vector<wxTreeItemId> m_tree_items;
    int  tree_selection() const;          // selected feature row, or wxNOT_FOUND
    void set_tree_selection(int row);
    static int tree_icon_for(CadFeatureType t);

    wxStaticText*     m_status{nullptr};
    wxStaticText*     m_dof_status{nullptr};   // DoF / constraint-state readout (P3)
    int               m_feature_counter{0};

    std::vector<wxButton*> m_confirm_btns;

    // Edit-in-place state: add-mode is m_edit_index == -1. Single-feature edit
    // (Sketch or Extrude independently) uses only m_edit_index as the row to replace.
    int               m_edit_index{-1};

    // Tree row of the sketch currently being constrained (-1 = not constraining).
    int               m_constrain_feat{-1};

    // Constraint-manager card (C3.4): header + a rebuildable list of constraint rows.
    wxSizer*          m_box_constraints{nullptr};
    wxStaticText*     m_hdr_constraints{nullptr};
    wxSizer*          m_constraint_rows{nullptr};
    int               m_constraint_sel{-1};   // highlighted constraint row, or -1
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignPanel_hpp_
