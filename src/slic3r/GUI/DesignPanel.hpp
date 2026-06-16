#ifndef slic3r_DesignPanel_hpp_
#define slic3r_DesignPanel_hpp_

#include <wx/panel.h>

namespace Slic3r { namespace GUI {

// P0 scaffold for the Design (CAD) tab. Minimal: a toolbar placeholder + a button that
// commits a test box to the Prepare plate via the existing model bridge. The real CAD
// canvas/feature-tree arrive in later phases.
class DesignPanel : public wxPanel
{
public:
    explicit DesignPanel(wxWindow* parent);

private:
    void commit_test_box();
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignPanel_hpp_
