#include "DesignPanel.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include "libslic3r/GeometryEngine.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

namespace Slic3r { namespace GUI {

DesignPanel::DesignPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, _L("Design (CAD) — work in progress")),
               0, wxALL, 12);

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    const char* tools[] = {"Sketch", "Extrude", "Fillet", "Chamfer", "Hole", "Thread"};
    for (const char* name : tools) {
        auto* b = new wxButton(this, wxID_ANY, name);
        b->Disable(); // placeholders until their phase lands
        toolbar->Add(b, 0, wxALL, 4);
    }
    sizer->Add(toolbar, 0, wxALL, 8);

    auto* commit = new wxButton(this, wxID_ANY, _L("Commit Test Box to Plate"));
    commit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { commit_test_box(); });
    sizer->Add(commit, 0, wxALL, 12);

    SetSizer(sizer);
}

void DesignPanel::commit_test_box()
{
    PrimitiveParams params; // defaults: 20 mm box
    TopoDS_Solid solid = GeometryEngine::make_primitive(params);
    TriangleMesh mesh = GeometryEngine::tessellate(solid, params.linear_deflection,
                                                   params.angular_deflection);
    if (mesh.its.indices.empty())
        return;

    ObjectList* obj_list = wxGetApp().obj_list();
    if (obj_list == nullptr)
        return;
    obj_list->load_mesh_object(mesh, "Design Box");

    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
}

}} // namespace Slic3r::GUI
