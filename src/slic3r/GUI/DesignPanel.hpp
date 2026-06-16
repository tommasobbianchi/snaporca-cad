#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <wx/panel.h>
#include <wx/scrolwin.h>

#include "libslic3r/CadDocument.hpp"

class wxChoice;
class wxCheckBox;
class wxSpinCtrlDouble;
class wxListBox;
class wxStaticText;

namespace Slic3r { namespace GUI {

class DesignViewport;

// Design (CAD) tab: a form-driven sketch + extrude + fillet/chamfer + hole that
// feeds a CadDocument feature tree, then commits the resulting solid to the
// Prepare plate via the model bridge. Interactive GL picking is a later refinement.
class DesignPanel : public wxPanel
{
public:
    explicit DesignPanel(wxWindow* parent);

private:
    void on_shape_changed();
    void on_add_feature();
    void on_add_dressup();
    void on_add_hole();
    void on_add_thread();
    void on_commit();
    void refresh_tree();
    void set_status_ok();

    CadDocument m_doc;

    wxScrolledWindow* m_form{nullptr};
    DesignViewport*   m_viewport{nullptr};

    wxChoice*         m_shape{nullptr};
    wxChoice*         m_plane{nullptr};
    wxChoice*         m_mode{nullptr};
    wxSpinCtrlDouble* m_width{nullptr};
    wxSpinCtrlDouble* m_height{nullptr};
    wxSpinCtrlDouble* m_radius{nullptr};
    wxSpinCtrlDouble* m_distance{nullptr};

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
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignPanel_hpp_
