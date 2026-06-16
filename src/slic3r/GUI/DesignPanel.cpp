#include "DesignPanel.hpp"
#include "DesignViewport.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/listbox.h>

#include <string>
#include <cmath>

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

// Inverse of plane_from_index: recover the wxChoice row from a plane's normal.
// XY normal=(0,0,1)->0, XZ normal=(0,1,0)->1, YZ normal=(1,0,0)->2.
static int index_from_plane(const SketchPlane& p)
{
    if (std::abs(p.normal.y()) > 0.5) return 1; // XZ
    if (std::abs(p.normal.x()) > 0.5) return 2; // YZ
    return 0;                                   // XY
}

DesignPanel::DesignPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    // Left column: the scrollable parameter form. All controls below are parented
    // to m_form so the form can scroll independently of the live GL viewport.
    m_form = new wxScrolledWindow(this, wxID_ANY);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(m_form, wxID_ANY, _L("Design (CAD) — Sketch + Extrude + Fillet/Chamfer + Hole + Thread")),
              0, wxALL, 12);

    // Toolbar of trigger buttons (declare intent; each opens its tool dialog).
    auto* tbar = new wxBoxSizer(wxVERTICAL);
    auto* b_sketch = new wxButton(m_form, wxID_ANY, _L("Sketch + Extrude"));
    b_sketch->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Sketch); });
    tbar->Add(b_sketch, 0, wxEXPAND | wxBOTTOM, 4);
    auto* b_dressup = new wxButton(m_form, wxID_ANY, _L("Fillet / Chamfer"));
    b_dressup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Dressup); });
    tbar->Add(b_dressup, 0, wxEXPAND | wxBOTTOM, 4);
    auto* b_hole = new wxButton(m_form, wxID_ANY, _L("Hole"));
    b_hole->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Hole); });
    tbar->Add(b_hole, 0, wxEXPAND | wxBOTTOM, 4);
    auto* b_thread = new wxButton(m_form, wxID_ANY, _L("Thread"));
    b_thread->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Thread); });
    tbar->Add(b_thread, 0, wxEXPAND | wxBOTTOM, 4);
    root->Add(tbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* form = new wxFlexGridSizer(2, 6, 8);

    m_shape = new wxChoice(m_form, wxID_ANY);
    m_shape->Append("Rectangle");
    m_shape->Append("Circle");
    m_shape->SetSelection(0);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Shape")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_shape);

    m_plane = new wxChoice(m_form, wxID_ANY);
    m_plane->Append("XY");
    m_plane->Append("XZ");
    m_plane->Append("YZ");
    m_plane->SetSelection(0);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Plane")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_plane);

    m_width = make_spin(m_form, 20);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Width / X")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_width);

    m_height = make_spin(m_form, 20);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Height / Y")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_height);

    m_radius = make_spin(m_form, 10);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_radius);

    m_distance = make_spin(m_form, 10);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Extrude dist")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_distance);

    m_mode = new wxChoice(m_form, wxID_ANY);
    m_mode->Append("New");
    m_mode->Append("Add");
    m_mode->Append("Cut");
    m_mode->SetSelection(0);
    form->Add(new wxStaticText(m_form, wxID_ANY, _L("Mode")), 0, wxALIGN_CENTER_VERTICAL);
    form->Add(m_mode);

    m_box_sketch = new wxBoxSizer(wxVERTICAL);
    m_box_sketch->Add(form, 0, wxALL, 12);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_tool(); });
        m_confirm_btns.push_back(ok);
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_tool(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_sketch->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }
    root->Add(m_box_sketch, 0, wxEXPAND);

    // --- Dress-up (Fillet / Chamfer) ---
    auto* dform = new wxFlexGridSizer(2, 6, 8);

    m_dressup_type = new wxChoice(m_form, wxID_ANY);
    m_dressup_type->Append("Fillet");
    m_dressup_type->Append("Chamfer");
    m_dressup_type->SetSelection(0);
    dform->Add(new wxStaticText(m_form, wxID_ANY, _L("Dress-up")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_type);

    m_face_group = new wxChoice(m_form, wxID_ANY);
    m_face_group->Append("Top");      // index 0 -> FaceGroup::Top
    m_face_group->Append("Bottom");   // 1 -> Bottom
    m_face_group->Append("Lateral");  // 2 -> Lateral
    m_face_group->Append("All");      // 3 -> All
    m_face_group->SetSelection(3);
    dform->Add(new wxStaticText(m_form, wxID_ANY, _L("Edges")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_face_group);

    m_dressup_size = make_spin(m_form, 2.0);
    dform->Add(new wxStaticText(m_form, wxID_ANY, _L("Size (r/dist)")), 0, wxALIGN_CENTER_VERTICAL);
    dform->Add(m_dressup_size);

    m_box_dressup = new wxBoxSizer(wxVERTICAL);
    m_box_dressup->Add(dform, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_tool(); });
        m_confirm_btns.push_back(ok);
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_tool(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_dressup->Add(row, 0, wxALL, 12);
    }
    root->Add(m_box_dressup, 0, wxEXPAND);

    // --- Hole (positioned circular cut) ---
    auto* hform = new wxFlexGridSizer(2, 6, 8);

    m_hole_plane = new wxChoice(m_form, wxID_ANY);
    m_hole_plane->Append("XY");
    m_hole_plane->Append("XZ");
    m_hole_plane->Append("YZ");
    m_hole_plane->SetSelection(0);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Hole plane")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_plane);

    m_hole_diameter = make_spin(m_form, 6.0);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Diameter")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_diameter);

    m_hole_depth = make_spin(m_form, 10.0);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Depth (blind)")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_depth);

    m_hole_x = make_spin(m_form, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_x);

    m_hole_y = make_spin(m_form, 0.0, -1000.0, 1000.0);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_y);

    m_hole_through = new wxCheckBox(m_form, wxID_ANY, _L("Through"));
    m_hole_through->SetValue(true);
    hform->Add(new wxStaticText(m_form, wxID_ANY, _L("Mode")), 0, wxALIGN_CENTER_VERTICAL);
    hform->Add(m_hole_through);

    m_box_hole = new wxBoxSizer(wxVERTICAL);
    m_box_hole->Add(hform, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_tool(); });
        m_confirm_btns.push_back(ok);
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_tool(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_hole->Add(row, 0, wxALL, 12);
    }
    root->Add(m_box_hole, 0, wxEXPAND);

    // --- Thread (helical) ---
    auto* tform = new wxFlexGridSizer(2, 6, 8);

    m_thread_plane = new wxChoice(m_form, wxID_ANY);
    m_thread_plane->Append("XY");
    m_thread_plane->Append("XZ");
    m_thread_plane->Append("YZ");
    m_thread_plane->SetSelection(0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Thread plane")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_plane);

    m_thread_radius = make_spin(m_form, 5.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Radius")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_radius);

    m_thread_pitch = make_spin(m_form, 2.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Pitch")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_pitch);

    m_thread_height = make_spin(m_form, 10.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Length")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_height);

    m_thread_depth = make_spin(m_form, 1.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Thread depth")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_depth);

    m_thread_x = make_spin(m_form, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Pos X")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_x);

    m_thread_y = make_spin(m_form, 0.0, -1000.0, 1000.0);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Pos Y")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_y);

    m_thread_internal = new wxCheckBox(m_form, wxID_ANY, _L("Internal (tapped bore)"));
    m_thread_internal->SetValue(false);
    tform->Add(new wxStaticText(m_form, wxID_ANY, _L("Internal")), 0, wxALIGN_CENTER_VERTICAL);
    tform->Add(m_thread_internal);

    m_box_thread = new wxBoxSizer(wxVERTICAL);
    m_box_thread->Add(tform, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_tool(); });
        m_confirm_btns.push_back(ok);
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_tool(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_thread->Add(row, 0, wxALL, 12);
    }
    root->Add(m_box_thread, 0, wxEXPAND);

    root->Add(new wxStaticText(m_form, wxID_ANY, _L("Feature tree")), 0, wxLEFT | wxTOP, 12);
    m_tree = new wxListBox(m_form, wxID_ANY, wxDefaultPosition, wxSize(-1, 140));
    root->Add(m_tree, 0, wxEXPAND | wxALL, 12);

    // Feature-tree edit row: act on the selected feature (delete / reorder).
    {
        auto* trow = new wxBoxSizer(wxHORIZONTAL);
        auto* edit = new wxButton(m_form, wxID_ANY, _L("Edit"));
        edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_edit_feature(); });
        auto* del  = new wxButton(m_form, wxID_ANY, _L("Delete"));
        del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_delete_feature(); });
        auto* up   = new wxButton(m_form, wxID_ANY, _L("↑"), wxDefaultPosition, wxSize(40, -1));
        up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(-1); });
        auto* down = new wxButton(m_form, wxID_ANY, _L("↓"), wxDefaultPosition, wxSize(40, -1));
        down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(+1); });
        trow->Add(edit, 0, wxRIGHT, 8);
        trow->Add(del, 0, wxRIGHT, 8);
        trow->Add(up,  0, wxRIGHT, 4);
        trow->Add(down, 0);
        root->Add(trow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }

    m_status = new wxStaticText(m_form, wxID_ANY, "");
    root->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    auto* commit = new wxButton(m_form, wxID_ANY, _L("Commit to Plate"));
    commit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_commit(); });
    root->Add(commit, 0, wxALL, 12);

    m_shape->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) { on_shape_changed(); e.Skip(); });
    on_shape_changed();

    // Any parameter edit refreshes the translucent preview. Command events from the
    // spin/choice/checkbox children propagate up to m_form, so one binding each suffices.
    m_form->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent& e) { refresh_preview(); e.Skip(); });
    m_form->Bind(wxEVT_CHOICE,         [this](wxCommandEvent& e)    { refresh_preview(); e.Skip(); });
    m_form->Bind(wxEVT_CHECKBOX,       [this](wxCommandEvent& e)    { refresh_preview(); e.Skip(); });

    m_form->SetSizer(root);

    // Start with every tool dialog hidden (only the toolbar + tree + Commit show).
    root->Show(m_box_sketch,  false, true);
    root->Show(m_box_dressup, false, true);
    root->Show(m_box_hole,    false, true);
    root->Show(m_box_thread,  false, true);

    m_form->FitInside();
    m_form->SetScrollRate(10, 10);
    m_form->SetMinSize(wxSize(380, -1));

    // Right column: a small view toolbar over the live 3D viewport that mirrors
    // the CadDocument body.
    m_viewport = new DesignViewport(this);

    auto* vcol = new wxBoxSizer(wxVERTICAL);
    auto* vbar = new wxBoxSizer(wxHORIZONTAL);
    auto* b_fit = new wxButton(this, wxID_ANY, _L("⊹ Fit view"));
    b_fit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_viewport->fit_view(); });
    vbar->Add(b_fit, 0, wxRIGHT, 8);
    // Standard-view shortcuts (a minimal view-cube): snap the camera to a named
    // orientation, reusing Camera::select_view via DesignViewport::set_view.
    struct { const char* label; const char* view; } views[] = {
        { "Iso", "iso" }, { "Front", "front" }, { "Top", "top" }, { "Right", "right" },
    };
    for (const auto& v : views) {
        std::string view = v.view;
        // Auto-size: translated labels (e.g. IT "Dall'alto") overflow a fixed width.
        auto* b = new wxButton(this, wxID_ANY, _L(v.label));
        b->Bind(wxEVT_BUTTON, [this, view](wxCommandEvent&) { m_viewport->set_view(view); });
        vbar->Add(b, 0, wxRIGHT, 4);
    }
    vcol->Add(vbar, 0, wxALL, 4);
    vcol->Add(m_viewport, 1, wxEXPAND);

    auto* outer = new wxBoxSizer(wxHORIZONTAL);
    outer->Add(m_form, 0, wxEXPAND);
    outer->Add(vcol,   1, wxEXPAND);
    SetSizer(outer);
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
    if (m_viewport != nullptr)
        m_viewport->set_mesh(m_doc.display_mesh);
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

void DesignPanel::after_tree_edit(bool ok)
{
    refresh_tree();
    if (!ok) {
        // The edit was rolled back (recompute failed); the body is unchanged.
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Edit rejected: ") + wxString::FromUTF8(m_doc.error));
        m_status->Refresh();
        return;
    }
    m_status->SetForegroundColour(wxNullColour);
    if (m_doc.display_mesh.its.indices.empty()) {
        if (m_viewport != nullptr) m_viewport->clear_mesh();
        m_status->SetLabel(wxString());
    } else {
        set_status_ok();
    }
    m_status->Refresh();
}

void DesignPanel::on_delete_feature()
{
    int sel = m_tree->GetSelection();
    if (sel == wxNOT_FOUND) {
        m_status->SetLabel(_L("Select a feature in the tree first"));
        m_status->Refresh();
        return;
    }
    after_tree_edit(m_doc.remove_feature(sel));
}

void DesignPanel::on_move_feature(int delta)
{
    int sel = m_tree->GetSelection();
    if (sel == wxNOT_FOUND) {
        m_status->SetLabel(_L("Select a feature in the tree first"));
        m_status->Refresh();
        return;
    }
    int target = sel + delta;
    if (target < 0 || target >= int(m_doc.features.size()))
        return; // already at the end
    if (m_doc.move_feature(sel, delta)) {
        after_tree_edit(true);
        m_tree->SetSelection(target); // keep the moved feature selected
    } else {
        after_tree_edit(false);
    }
}

void DesignPanel::reset_edit_state()
{
    m_edit_index   = -1;
    m_edit_sketch  = -1;
    m_edit_extrude = -1;
}

void DesignPanel::load_feature_into_dialog(const CadFeature& f, const CadFeature* extrude)
{
    switch (f.type) {
    case CadFeatureType::Sketch:
    case CadFeatureType::Extrude:
        m_shape->SetSelection(f.shape == SketchShape::Circle ? 1 : 0);
        m_plane->SetSelection(index_from_plane(f.plane));
        m_width->SetValue(f.width);
        m_height->SetValue(f.height);
        m_radius->SetValue(f.radius);
        if (extrude != nullptr) {
            m_distance->SetValue(extrude->distance);
            m_mode->SetSelection(static_cast<int>(extrude->mode)); // New=0,Add=1,Cut=2
        }
        break;
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
        m_dressup_type->SetSelection(f.type == CadFeatureType::Fillet ? 0 : 1);
        m_dressup_size->SetValue(f.dressup_size);
        m_face_group->SetSelection(static_cast<int>(f.face_group));
        break;
    case CadFeatureType::Hole:
        m_hole_plane->SetSelection(index_from_plane(f.plane));
        m_hole_diameter->SetValue(f.hole_diameter);
        m_hole_depth->SetValue(f.hole_depth);
        m_hole_through->SetValue(f.hole_through);
        m_hole_x->SetValue(f.hole_x);
        m_hole_y->SetValue(f.hole_y);
        break;
    case CadFeatureType::Thread:
        m_thread_plane->SetSelection(index_from_plane(f.plane));
        m_thread_radius->SetValue(f.thread_radius);
        m_thread_pitch->SetValue(f.thread_pitch);
        m_thread_height->SetValue(f.thread_height);
        m_thread_depth->SetValue(f.thread_depth);
        m_thread_internal->SetValue(f.thread_internal);
        m_thread_x->SetValue(f.thread_x);
        m_thread_y->SetValue(f.thread_y);
        break;
    }
}

void DesignPanel::on_edit_feature()
{
    int sel = m_tree->GetSelection();
    if (sel == wxNOT_FOUND) {
        m_status->SetLabel(_L("Select a feature in the tree first"));
        m_status->Refresh();
        return;
    }
    const CadFeature& f = m_doc.features[sel];
    reset_edit_state();

    switch (f.type) {
    case CadFeatureType::Sketch:
    case CadFeatureType::Extrude: {
        // A box is a Sketch + its Extrude. Resolve the linked pair regardless of
        // which of the two rows was selected, then edit both together.
        int sketch_idx = -1, extrude_idx = -1;
        if (f.type == CadFeatureType::Extrude) {
            extrude_idx = sel;
            sketch_idx  = f.sketch_ref;
        } else {
            sketch_idx = sel;
            for (int i = 0; i < int(m_doc.features.size()); ++i)
                if (m_doc.features[i].type == CadFeatureType::Extrude &&
                    m_doc.features[i].sketch_ref == sel) { extrude_idx = i; break; }
        }
        if (sketch_idx < 0 || extrude_idx < 0 ||
            sketch_idx >= int(m_doc.features.size())) {
            m_status->SetForegroundColour(wxColour(235, 110, 110));
            m_status->SetLabel(_L("This sketch has no extrude to edit"));
            m_status->Refresh();
            return;
        }
        m_edit_sketch  = sketch_idx;
        m_edit_extrude = extrude_idx;
        load_feature_into_dialog(m_doc.features[sketch_idx], &m_doc.features[extrude_idx]);
        open_tool(Tool::Sketch);
        break;
    }
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
        m_edit_index = sel;
        load_feature_into_dialog(f, nullptr);
        open_tool(Tool::Dressup);
        break;
    case CadFeatureType::Hole:
        m_edit_index = sel;
        load_feature_into_dialog(f, nullptr);
        open_tool(Tool::Hole);
        break;
    case CadFeatureType::Thread:
        m_edit_index = sel;
        load_feature_into_dialog(f, nullptr);
        open_tool(Tool::Thread);
        break;
    }
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

CadFeature DesignPanel::build_candidate(Tool t) const
{
    CadFeature f;
    switch (t) {
    case Tool::Sketch:
        f.type       = CadFeatureType::Extrude;
        f.sketch_ref = -1; // self-contained: apply_feature uses f's inline sketch params
        f.shape      = (m_shape->GetSelection() == 0) ? SketchShape::Rectangle : SketchShape::Circle;
        f.plane      = plane_from_index(m_plane->GetSelection());
        f.width      = m_width->GetValue();
        f.height     = m_height->GetValue();
        f.radius     = m_radius->GetValue();
        f.distance   = m_distance->GetValue();
        f.symmetric  = false;
        f.mode       = (m_mode->GetSelection() == 0) ? BooleanMode::New
                     : (m_mode->GetSelection() == 1) ? BooleanMode::Add
                                                     : BooleanMode::Cut;
        break;
    case Tool::Dressup:
        f.type         = (m_dressup_type->GetSelection() == 0) ? CadFeatureType::Fillet
                                                               : CadFeatureType::Chamfer;
        f.dressup_size = m_dressup_size->GetValue();
        f.face_group   = static_cast<FaceGroup>(m_face_group->GetSelection());
        break;
    case Tool::Hole:
        f.type          = CadFeatureType::Hole;
        f.plane         = plane_from_index(m_hole_plane->GetSelection());
        f.hole_diameter = m_hole_diameter->GetValue();
        f.hole_depth    = m_hole_depth->GetValue();
        f.hole_through  = m_hole_through->GetValue();
        f.hole_x        = m_hole_x->GetValue();
        f.hole_y        = m_hole_y->GetValue();
        break;
    case Tool::Thread:
        f.type            = CadFeatureType::Thread;
        f.plane           = plane_from_index(m_thread_plane->GetSelection());
        f.thread_radius   = m_thread_radius->GetValue();
        f.thread_pitch    = m_thread_pitch->GetValue();
        f.thread_height   = m_thread_height->GetValue();
        f.thread_depth    = m_thread_depth->GetValue();
        f.thread_internal = m_thread_internal->GetValue();
        f.thread_x        = m_thread_x->GetValue();
        f.thread_y        = m_thread_y->GetValue();
        break;
    case Tool::None:
        break;
    }
    return f;
}

void DesignPanel::refresh_preview()
{
    if (m_active == Tool::None) { m_viewport->clear_preview(); return; }

    CadFeature   cand = build_candidate(m_active);
    TriangleMesh mesh;
    std::string  err;
    bool         ok = false;

    const bool editing_pair   = (m_edit_sketch >= 0 && m_edit_extrude >= 0);
    const bool editing_single = (m_edit_index >= 0);
    if (editing_pair || editing_single) {
        // Edit-mode preview: stacking the candidate on top of the live body would
        // re-apply the feature being edited (fillet-on-fillet) — wrong, and a
        // source of OCCT failures. Instead evaluate the *replace* on a throwaway
        // copy so the ghost is the true post-edit body.
        CadDocument tmp = m_doc;
        ok = editing_pair
           ? tmp.replace_sketch_extrude(m_edit_sketch, m_edit_extrude, cand)
           : tmp.replace_feature(m_edit_index, cand);
        if (ok) mesh = tmp.display_mesh; else err = tmp.error;
    } else {
        ok = m_doc.preview(cand, mesh, err);
    }

    if (ok) {
        m_viewport->set_preview_mesh(mesh);
        m_status->SetForegroundColour(wxColour(120, 210, 120)); // ok = green
        m_status->SetLabel(wxString::Format(_L("Preview — %zu triangles"), mesh.its.indices.size()));
    } else {
        m_viewport->clear_preview();
        m_status->SetForegroundColour(wxColour(235, 110, 110)); // invalid = red
        m_status->SetLabel(_L("Invalid: ") + wxString::FromUTF8(err));
    }
    // Onshape parity: a broken candidate cannot be committed. Grey the active dialog's
    // Confirm so the user sees the gate before clicking; the red status says why.
    for (wxButton* b : m_confirm_btns)
        if (b != nullptr) b->Enable(ok);
    m_status->Refresh();
}

void DesignPanel::open_tool(Tool t)
{
    m_active = t;
    wxSizer* s = m_form->GetSizer();
    s->Show(m_box_sketch,  t == Tool::Sketch,  true);
    s->Show(m_box_dressup, t == Tool::Dressup, true);
    s->Show(m_box_hole,    t == Tool::Hole,    true);
    s->Show(m_box_thread,  t == Tool::Thread,  true);
    m_form->Layout();
    m_form->FitInside();
    refresh_preview();
}

void DesignPanel::close_tool()
{
    m_active = Tool::None;
    wxSizer* s = m_form->GetSizer();
    s->Show(m_box_sketch,  false, true);
    s->Show(m_box_dressup, false, true);
    s->Show(m_box_hole,    false, true);
    s->Show(m_box_thread,  false, true);
    m_viewport->clear_preview();
    m_form->Layout();
    m_form->FitInside();
}

void DesignPanel::confirm_tool()
{
    const bool editing_pair   = (m_edit_sketch >= 0 && m_edit_extrude >= 0);
    const bool editing_single = (m_edit_index >= 0);

    if (editing_pair || editing_single) {
        // Edit mode: overwrite the existing feature(s) instead of appending.
        CadFeature cand = build_candidate(m_active);
        bool ok = editing_pair
                ? m_doc.replace_sketch_extrude(m_edit_sketch, m_edit_extrude, cand)
                : m_doc.replace_feature(m_edit_index, cand);
        reset_edit_state();
        close_tool();          // clears the preview ghost
        after_tree_edit(ok);   // refresh tree/viewport/status (or "Edit rejected")
        return;
    }

    switch (m_active) {
    case Tool::Sketch:  on_add_feature(); break;
    case Tool::Dressup: on_add_dressup(); break;
    case Tool::Hole:    on_add_hole();    break;
    case Tool::Thread:  on_add_thread();  break;
    case Tool::None:    return;
    }
    close_tool(); // also clears the preview ghost; the committed body is now shown
}

void DesignPanel::cancel_tool()
{
    reset_edit_state(); // abort an in-progress edit: back to add-mode
    close_tool();
    // Cancel discards the candidate: clear the stale "Preview …"/"Invalid …"
    // label and restore the neutral idle colour (Confirm keeps its "OK" status).
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(wxString());
    m_status->Refresh();
}

}} // namespace Slic3r::GUI
