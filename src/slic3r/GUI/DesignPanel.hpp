#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <wx/panel.h>
#include <wx/scrolwin.h>

#include <vector>

#include "libslic3r/CadDocument.hpp"

class wxChoice;
class wxCheckBox;
class wxSpinCtrlDouble;
class wxListBox;
class wxStaticText;
class wxSizer;
class wxButton;

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

    void on_shape_changed();
    void on_add_sketch();
    void on_add_extrude();
    void on_add_dressup();
    void on_add_hole();
    void on_add_thread();
    void on_commit();
    void refresh_tree();
    void set_status_ok();

    // Feature-tree editing (Onshape-style): act on the selected tree row.
    void on_delete_feature();
    void on_move_feature(int delta);   // -1 = up, +1 = down

    // Constrain mode: enter on the tree-selected sketch, then apply a geometric
    // constraint to the in-canvas picked segment and re-solve in the kernel.
    void on_begin_constrain();
    void apply_constraint(SketchConstraintType type);
    void apply_entity_constraint(SketchConstraintType type);  // Fase 4.2 entity path
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

    CadDocument m_doc;

    Tool      m_active{Tool::None};
    wxSizer*  m_box_sketch{nullptr};
    wxSizer*  m_box_extrude{nullptr};
    wxSizer*  m_box_dressup{nullptr};
    wxSizer*  m_box_hole{nullptr};
    wxSizer*  m_box_thread{nullptr};

    wxScrolledWindow* m_form{nullptr};
    DesignCanvas*     m_viewport{nullptr};

    wxChoice*         m_draw_plane{nullptr};
    wxChoice*         m_shape{nullptr};
    wxChoice*         m_plane{nullptr};
    wxChoice*         m_mode{nullptr};
    wxSpinCtrlDouble* m_width{nullptr};
    wxSpinCtrlDouble* m_height{nullptr};
    wxSpinCtrlDouble* m_radius{nullptr};
    wxSpinCtrlDouble* m_distance{nullptr};

    wxStaticText*     m_extrude_sketch_label{nullptr};
    int               m_extrude_sketch_ref{-1};

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

    wxListBox*        m_tree{nullptr};
    wxStaticText*     m_status{nullptr};
    int               m_feature_counter{0};

    std::vector<wxButton*> m_confirm_btns;

    // Edit-in-place state: add-mode is m_edit_index == -1. Single-feature edit
    // (Sketch or Extrude independently) uses only m_edit_index as the row to replace.
    int               m_edit_index{-1};

    // Tree row of the sketch currently being constrained (-1 = not constraining).
    int               m_constrain_feat{-1};
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignPanel_hpp_
