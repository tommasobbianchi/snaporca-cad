#include "DesignPanel.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/listbox.h>

#include <string>

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

namespace Slic3r { namespace GUI {

static wxSpinCtrlDouble* make_spin(wxWindow* parent, double val,
                                   double mn = 0.1, double mx = 1000.0)
{
    auto* s = new wxSpinCtrlDouble(parent, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1));
    s->SetRange(mn, mx);
    s->SetDigits(2);
    s->SetValue(val);
    return s;
}

static SketchPlane plane_from_index(int i)
{
    switch (i) {
        case 1:  return SketchPlane::XZ();
        case 2:  return SketchPlane::YZ();
        default: return SketchPlane::XY();
    }
}

DesignPanel::DesignPanel(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY, _L("Design (CAD) — Sketch + Extrude + Fillet/Chamfer + Hole + Thread")),
              0, wxALL, 12);

    auto* form = new wxFlexGridSizer(2, 6, 8);

    m_shape = new wxChoice(this, wxID_ANY);
    m_shape->Append("Rectangle");
    m_shape->Append("Circle");
    m_shape->SetSelection(0);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Shape")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_shape);

    m_plane = new wxChoice(this, wxID_ANY);
    m_plane->Append("XY");
    m_plane->Append("XZ");
    m_plane->Append("YZ");
    m_plane->SetSelection(0);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Plane")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_plane);

    m_width = make_spin(this, 20);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Width / X")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_width);

    m_height = make_spin(this, 20);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Height / Y")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_height);

    m_radius = make_spin(this, 10);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_radius);

    m_distance = make_spin(this, 10);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Extrude dist")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_distance);

    m_mode = new wxChoice(this, wxID_ANY);
    m_mode->Append("New");
    m_mode->Append("Add");
    m_mode->Append("Cut");
    m_mode->SetSelection(0);
    form->Add(new wxStaticText(this, wxID_ANY, _L("Mode")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_mode);

    root->Add(form, 0, wxALL, 12);

    auto* add = new wxButton(this, wxID_ANY, _L("Add Sketch + Extrude"));
    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_add_feature(); });
    root->Add(add, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // --- Dress-up (Fillet / Chamfer) ---
    auto* dform = new wxFlexGridSizer(2, 6, 8);

    m_dressup_type = new wxChoice(this, wxID_ANY);
    m_dressup_type->Append("Fillet");
    m_dressup_type->Append("Chamfer");
    m_dressup_type->SetSelection(0);
    dform->Add(new wxStaticText(this, wxID_ANY, _L("Dress-up")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_type);

    m_face_group = new wxChoice(this, wxID_ANY);
    m_face_group->Append("Top");      // index 0 -> FaceGroup::Top
    m_face_group->Append("Bottom");   // 1 -> Bottom
    m_face_group->Append("Lateral");  // 2 -> Lateral
    m_face_group->Append("All");      // 3 -> All
    m_face_group->SetSelection(3);
    dform->Add(new wxStaticText(this, wxID_ANY, _L("Edges")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_face_group);

    m_dressup_size = make_spin(this, 2.0);
    dform->Add(new wxStaticText(this, wxID_ANY, _L("Size (r/dist)")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_size);

    root->Add(dform, 0, wxLEFT | wxRIGHT, 12);

    auto* add_du = new wxButton(this, wxID_ANY, _L("Add Fillet/Chamfer"));
    add_du->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_add_dressup(); });
    root->Add(add_du, 0, wxALL, 12);

    // --- Hole (positioned circular cut) ---
    auto* hform = new wxFlexGridSizer(2, 6, 8);

    m_hole_plane = new wxChoice(this, wxID_ANY);
    m_hole_plane->Append("XY");
    m_hole_plane->Append("XZ");
    m_hole_plane->Append("YZ");
    m_hole_plane->SetSelection(0);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Hole plane")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_plane);

    m_hole_diameter = make_spin(this, 6.0);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Diameter")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_diameter);

    m_hole_depth = make_spin(this, 10.0);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Depth (blind)")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_depth);

    m_hole_x = make_spin(this, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_x);

    m_hole_y = make_spin(this, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_y);

    m_hole_through = new wxCheckBox(this, wxID_ANY, _L("Through"));
    m_hole_through->SetValue(true);
    hform->Add(new wxStaticText(this, wxID_ANY, _L("Mode")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_through);

    root->Add(hform, 0, wxLEFT | wxRIGHT, 12);

    auto* add_hole = new wxButton(this, wxID_ANY, _L("Add Hole"));
    add_hole->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_add_hole(); });
    root->Add(add_hole, 0, wxALL, 12);

    // --- Thread (helical) ---
    auto* tform = new wxFlexGridSizer(2, 6, 8);

    m_thread_plane = new wxChoice(this, wxID_ANY);
    m_thread_plane->Append("XY");
    m_thread_plane->Append("XZ");
    m_thread_plane->Append("YZ");
    m_thread_plane->SetSelection(0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Thread plane")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_plane);

    m_thread_radius = make_spin(this, 5.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_radius);

    m_thread_pitch = make_spin(this, 2.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Pitch")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_pitch);

    m_thread_height = make_spin(this, 10.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Length")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_height);

    m_thread_depth = make_spin(this, 1.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Thread depth")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_depth);

    m_thread_x = make_spin(this, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_x);

    m_thread_y = make_spin(this, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_y);

    m_thread_internal = new wxCheckBox(this, wxID_ANY, _L("Internal (tapped bore)"));
    m_thread_internal->SetValue(false);
    tform->Add(new wxStaticText(this, wxID_ANY, _L("Internal")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_internal);

    root->Add(tform, 0, wxLEFT | wxRIGHT, 12);

    auto* add_thread = new wxButton(this, wxID_ANY, _L("Add Thread"));
    add_thread->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_add_thread(); });
    root->Add(add_thread, 0, wxALL, 12);

    root->Add(new wxStaticText(this, wxID_ANY, _L("Feature tree")), 0, wxLEFT | wxTOP, 12);
    m_tree = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 140));
    root->Add(m_tree, 0, wxEXPAND | wxALL, 12);

    m_status = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* commit = new wxButton(this, wxID_ANY, _L("Commit to Plate"));
    commit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_commit(); });
    root->Add(commit, 0, wxALL, 12);

    m_shape->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { on_shape_changed(); });
    on_shape_changed();

    SetSizer(root);
    FitInside();
    SetScrollRate(10, 10);
}

void DesignPanel::on_shape_changed()
{
    bool rect = (m_shape->GetSelection() == 0);
    m_width->Enable(rect);
    m_height->Enable(rect);
    m_radius->Enable(!rect);
}

void DesignPanel::set_status_ok()
{
    m_status->SetLabel(wxString::Format(_L("OK — %zu triangles"),
                                        m_doc.display_mesh.its.indices.size()));
}

void DesignPanel::on_add_feature()
{
    SketchShape shape = (m_shape->GetSelection() == 1) ? SketchShape::Circle
                                                       : SketchShape::Rectangle;
    SketchPlane plane = plane_from_index(m_plane->GetSelection());
    BooleanMode mode  = static_cast<BooleanMode>(m_mode->GetSelection()); // New=0, Add=1, Cut=2

    m_feature_counter++;
    std::string sname = "Sketch" + std::to_string(m_feature_counter);
    std::string ename = "Extrude" + std::to_string(m_feature_counter);

    int sref = m_doc.add_sketch(shape, plane,
                                m_width->GetValue(), m_height->GetValue(), m_radius->GetValue(),
                                sname);
    m_doc.add_extrude(sref, m_distance->GetValue(), false, mode, ename);

    if (!m_doc.recompute())
        m_status->SetLabel(_L("Recompute error: ") + wxString::FromUTF8(m_doc.error));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_dressup()
{
    if (m_doc.body.IsNull()) {
        m_status->SetLabel(_L("Add a solid (sketch + extrude) first"));
        return;
    }
    FaceGroup fg = static_cast<FaceGroup>(m_face_group->GetSelection()); // Top=0..All=3
    double    sz = m_dressup_size->GetValue();
    bool      fillet = (m_dressup_type->GetSelection() == 0);

    m_feature_counter++;
    if (fillet)
        m_doc.add_fillet(sz, fg, "Fillet" + std::to_string(m_feature_counter));
    else
        m_doc.add_chamfer(sz, fg, "Chamfer" + std::to_string(m_feature_counter));

    if (!m_doc.recompute())
        m_status->SetLabel(_L("Recompute error: ") + wxString::FromUTF8(m_doc.error));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_hole()
{
    if (m_doc.body.IsNull()) {
        m_status->SetLabel(_L("Add a solid (sketch + extrude) first"));
        return;
    }
    SketchPlane plane   = plane_from_index(m_hole_plane->GetSelection());
    double      dia     = m_hole_diameter->GetValue();
    double      depth   = m_hole_depth->GetValue();
    bool        through = m_hole_through->GetValue();
    double      px      = m_hole_x->GetValue();
    double      py      = m_hole_y->GetValue();

    m_feature_counter++;
    m_doc.add_hole(dia, depth, through, px, py, plane,
                   "Hole" + std::to_string(m_feature_counter));

    if (!m_doc.recompute())
        m_status->SetLabel(_L("Recompute error: ") + wxString::FromUTF8(m_doc.error));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::on_add_thread()
{
    bool internal = m_thread_internal->GetValue();
    if (internal && m_doc.body.IsNull()) {
        m_status->SetLabel(_L("Internal thread needs a body — add a solid first"));
        return;
    }
    SketchPlane plane = plane_from_index(m_thread_plane->GetSelection());

    m_feature_counter++;
    m_doc.add_thread(m_thread_radius->GetValue(), m_thread_pitch->GetValue(),
                     m_thread_height->GetValue(), m_thread_depth->GetValue(),
                     internal, m_thread_x->GetValue(), m_thread_y->GetValue(),
                     plane, "Thread" + std::to_string(m_feature_counter));

    if (!m_doc.recompute())
        m_status->SetLabel(_L("Recompute error: ") + wxString::FromUTF8(m_doc.error));
    else
        set_status_ok();

    refresh_tree();
}

void DesignPanel::refresh_tree()
{
    m_tree->Clear();
    for (const auto& f : m_doc.features)
        m_tree->Append(wxString::FromUTF8(f.name));
}

void DesignPanel::on_commit()
{
    if (m_doc.display_mesh.its.indices.empty()) {
        m_status->SetLabel(_L("Nothing to commit — add a feature first"));
        return;
    }
    ObjectList* obj_list = wxGetApp().obj_list();
    if (obj_list == nullptr)
        return;
    obj_list->load_mesh_object(m_doc.display_mesh, "Design Body");

    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
}

}} // namespace Slic3r::GUI
