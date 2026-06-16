#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <wx/panel.h>

#include "libslic3r/CadDocument.hpp"

class wxChoice;
class wxSpinCtrlDouble;
class wxListBox;
class wxStaticText;

namespace Slic3r { namespace GUI {

// Design (CAD) tab: a form-driven sketch + extrude + fillet/chamfer that feeds a
// CadDocument feature tree, then commits the resulting solid to the Prepare plate
// via the model bridge. Interactive GL-canvas sketching/picking is a later refinement.
class DesignPanel : public wxPanel
{
public:
    explicit DesignPanel(wxWindow* parent);

private:
    void on_shape_changed();
    void on_add_feature();
    void on_add_dressup();
    void on_commit();
    void refresh_tree();
    void set_status_ok();

    CadDocument m_doc;

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

    wxListBox*        m_tree{nullptr};
    wxStaticText*     m_status{nullptr};
    int               m_feature_counter{0};
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignPanel_hpp_
