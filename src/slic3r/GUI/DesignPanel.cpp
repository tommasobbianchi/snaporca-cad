#include "DesignPanel.hpp"
#include "DesignCanvas.hpp"
#include "DesignSketchTool.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/listbox.h>
#include <wx/textdlg.h>
#include <wx/statline.h>
#include <wx/font.h>

#include <string>
#include <cmath>

#include "slic3r/GUI/wxExtensions.hpp"   // ScalableButton, create_scaled_bitmap
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
    // Left column: a slim feature-tree + docked tool-dialog column. All form
    // controls are parented to m_form so it can scroll independently of the
    // live GL viewport. The tool buttons live in the top toolbar (built below).
    m_form = new wxScrolledWindow(this, wxID_ANY);

    auto* root = new wxBoxSizer(wxVERTICAL);
    {
        auto* hdr = new wxStaticText(m_form, wxID_ANY, _L("Design"));
        wxFont hf = hdr->GetFont();
        hf.SetPointSize(hf.GetPointSize() + 2);
        hf.SetWeight(wxFONTWEIGHT_BOLD);
        hdr->SetFont(hf);
        root->Add(hdr, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        root->AddSpacer(2);
    }

    // === Top contextual toolbar (Onshape-style icon strip) ===
    // Parented to the panel (sits above the form/viewport row). Only the active
    // mode's group is shown; the others are hidden by set_ui_mode().
    m_toolbar = new wxPanel(this, wxID_ANY);
    m_toolbar->SetBackgroundColour(wxColour(0x32, 0x32, 0x37));

    auto icon_btn = [this](const char* icon, const wxString& tip) {
        auto* b = new ScalableButton(m_toolbar, wxID_ANY, icon, "", wxSize(34, 34),
                                     wxDefaultPosition, wxBU_EXACTFIT | wxBORDER_NONE, false, 22);
        b->SetToolTip(tip);
        return b;
    };
    auto add_sep = [this](wxSizer* row) {
        row->AddSpacer(5);
        row->Add(new wxStaticLine(m_toolbar, wxID_ANY, wxDefaultPosition, wxSize(1, 22), wxLI_VERTICAL),
                 0, wxALIGN_CENTER_VERTICAL);
        row->AddSpacer(5);
    };

    // Shared sketch-tool selector: begins a session on first use, then switches
    // the active entity tool. The Construction toggle marks following entities as
    // construction geometry (excluded from the wire).
    m_construction = new wxCheckBox(m_toolbar, wxID_ANY, _L("Construction"));
    m_construction->SetForegroundColour(wxColour(0xC8, 0xC8, 0xC8));
    auto select_tool = [this](DesignSketchTool::Mode mode, const wxString& hint) {
        if (!m_viewport) return;
        if (!m_viewport->is_sketching()) {
            const SketchPlane plane = plane_from_index(m_draw_plane->GetSelection());
            m_viewport->begin_sketch(plane, mode);
            m_construction->SetValue(false);   // a fresh session starts non-construction
        } else {
            m_viewport->set_sketch_tool(mode);
        }
        m_viewport->set_sketch_construction(m_construction->GetValue());
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(hint);
        m_status->Refresh();
    };

    // --- Feature group: Sketch / Extrude / Fillet-Chamfer / Hole / Thread / Constrain
    m_tb_feature = new wxBoxSizer(wxHORIZONTAL);
    auto fadd = [this](wxWindow* w) { m_tb_feature->Add(w, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2); };
    {
        auto* b_sketch = icon_btn("design_sketch", _L("Sketch"));
        b_sketch->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            set_ui_mode(UiMode::Sketch);
            m_status->SetForegroundColour(wxNullColour);
            m_status->SetLabel(_L("Pick a plane and a sketch tool, then draw"));
            m_status->Refresh();
        });
        fadd(b_sketch);
        add_sep(m_tb_feature);
        auto* b_extrude = icon_btn("design_extrude", _L("Extrude"));
        b_extrude->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_extrude_sketch_ref = resolve_extrude_sketch();
            if (m_extrude_sketch_ref < 0) {
                m_status->SetForegroundColour(wxColour(235, 110, 110));
                m_status->SetLabel(_L("Create a sketch first"));
                m_status->Refresh();
                return;
            }
            open_tool(Tool::Extrude);
        });
        fadd(b_extrude);
        auto* b_dressup = icon_btn("design_dressup", _L("Fillet / Chamfer"));
        b_dressup->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Dressup); });
        fadd(b_dressup);
        auto* b_hole = icon_btn("design_hole", _L("Hole"));
        b_hole->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Hole); });
        fadd(b_hole);
        auto* b_thread = icon_btn("design_thread", _L("Thread"));
        b_thread->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tool(Tool::Thread); });
        fadd(b_thread);
        add_sep(m_tb_feature);
        auto* b_constrain = icon_btn("design_constrain", _L("Constrain selected sketch"));
        b_constrain->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            on_begin_constrain();
            if (m_viewport && (m_viewport->is_constraining() || m_viewport->is_constraining_entities()))
                set_ui_mode(UiMode::Constrain);
        });
        fadd(b_constrain);
    }

    // --- Sketch group: plane + entity tools + Construction + Finish
    m_tb_sketch = new wxBoxSizer(wxHORIZONTAL);
    auto sadd = [this](wxWindow* w) { m_tb_sketch->Add(w, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2); };
    {
        m_draw_plane = new wxChoice(m_toolbar, wxID_ANY);
        m_draw_plane->Append("XY"); m_draw_plane->Append("XZ"); m_draw_plane->Append("YZ");
        m_draw_plane->SetSelection(0);
        sadd(m_draw_plane);
        add_sep(m_tb_sketch);
        auto skbtn = [&](const char* icon, DesignSketchTool::Mode mode,
                         const wxString& tip, const wxString& hint) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [select_tool, mode, hint](wxCommandEvent&) { select_tool(mode, hint); });
            sadd(b);
        };
        skbtn("design_line",      DesignSketchTool::Mode::Polyline,         _L("Line"),
              _L("Click points; click first / right-click to close the loop"));
        skbtn("design_rect",      DesignSketchTool::Mode::CornerRect,       _L("Corner rectangle"),
              _L("Click two opposite corners"));
        skbtn("design_crect",     DesignSketchTool::Mode::CenterRect,       _L("Center rectangle"),
              _L("Click center, then a corner"));
        skbtn("design_circle",    DesignSketchTool::Mode::CenterCircle,     _L("Center circle"),
              _L("Click center, then radius"));
        skbtn("design_point",     DesignSketchTool::Mode::Point,            _L("Point"),
              _L("Click to place a point"));
        skbtn("design_circle3pt", DesignSketchTool::Mode::ThreePointCircle, _L("3-point circle"),
              _L("Click three points on the circle"));
        skbtn("design_arc3pt",    DesignSketchTool::Mode::ThreePointArc,    _L("3-point arc"),
              _L("Click start, end, then a point on the arc"));
        skbtn("design_tangentarc", DesignSketchTool::Mode::TangentArc,      _L("Tangent arc"),
              _L("Click start (on the last entity) then end"));
        skbtn("design_slot",      DesignSketchTool::Mode::Slot,             _L("Slot"),
              _L("Click two centerline ends, then a point for width"));

        m_sides = new wxSpinCtrl(m_toolbar, wxID_ANY, "6", wxDefaultPosition, wxSize(50, -1));
        m_sides->SetRange(3, 64);
        m_sides->SetValue(6);
        auto* b_poly = icon_btn("design_polygon", _L("Polygon"));
        b_poly->Bind(wxEVT_BUTTON, [this, select_tool](wxCommandEvent&) {
            if (m_viewport) m_viewport->set_sketch_polygon_sides(m_sides->GetValue());
            select_tool(DesignSketchTool::Mode::Polygon, _L("Click center then a vertex")); });
        m_sides->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
            if (m_viewport) m_viewport->set_sketch_polygon_sides(m_sides->GetValue()); });
        sadd(b_poly);
        sadd(m_sides);
        add_sep(m_tb_sketch);
        m_construction->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (m_viewport && m_viewport->is_sketching())
                m_viewport->set_sketch_construction(m_construction->GetValue()); });
        sadd(m_construction);
        add_sep(m_tb_sketch);
        auto* b_finish = icon_btn("design_check", _L("Finish sketch"));
        b_finish->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_viewport && m_viewport->is_sketching()) m_viewport->finish_sketch();
            set_ui_mode(UiMode::Feature); });
        sadd(b_finish);
    }

    // --- Constrain group: geometric constraints + dimensions + edit ops + Done
    m_tb_constrain = new wxBoxSizer(wxHORIZONTAL);
    auto cadd = [this](wxWindow* w) { m_tb_constrain->Add(w, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2); };
    {
        auto cbtn = [&](const char* icon, const wxString& tip, SketchConstraintType type) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [this, type](wxCommandEvent&) { apply_constraint(type); });
            cadd(b);
        };
        cbtn("design_c_horizontal",    _L("Horizontal"),    SketchConstraintType::Horizontal);
        cbtn("design_c_vertical",      _L("Vertical"),      SketchConstraintType::Vertical);
        cbtn("design_c_parallel",      _L("Parallel"),      SketchConstraintType::Parallel);
        cbtn("design_c_perpendicular", _L("Perpendicular"), SketchConstraintType::Perpendicular);
        cbtn("design_c_coincident",    _L("Coincident"),    SketchConstraintType::Coincident);
        cbtn("design_c_equal",         _L("Equal length"),  SketchConstraintType::EqualLength);
        cbtn("design_c_concentric",    _L("Concentric"),    SketchConstraintType::Concentric);
        cbtn("design_c_tangent",       _L("Tangent"),       SketchConstraintType::Tangent);
        cbtn("design_c_midpoint",      _L("Midpoint"),      SketchConstraintType::Midpoint);
        cbtn("design_c_angle",         _L("Angle"),         SketchConstraintType::Angle);
        cbtn("design_c_radius",        _L("Radius"),        SketchConstraintType::Radius);
        cbtn("design_c_diameter",      _L("Diameter"),      SketchConstraintType::Diameter);
        add_sep(m_tb_constrain);
        auto ebtn = [&](const char* icon, const wxString& tip, EditOp op) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [this, op](wxCommandEvent&) { apply_edit_op(op); });
            cadd(b);
        };
        ebtn("design_mirror",     _L("Mirror"),       EditOp::Mirror);
        ebtn("design_offset",     _L("Offset"),       EditOp::Offset);
        ebtn("design_filletedge", _L("Sketch fillet"),EditOp::Fillet);
        ebtn("design_trim",       _L("Trim"),         EditOp::Trim);
        ebtn("design_extend",     _L("Extend"),       EditOp::Extend);
        add_sep(m_tb_constrain);
        auto* b_done = icon_btn("design_check", _L("Done constraining"));
        b_done->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_constrain_feat = -1;
            set_ui_mode(UiMode::Feature);
            m_status->SetForegroundColour(wxNullColour);
            m_status->SetLabel(wxString());
            m_status->Refresh(); });
        cadd(b_done);
    }

    auto* tbrow = new wxBoxSizer(wxHORIZONTAL);
    tbrow->AddSpacer(8);
    tbrow->Add(m_tb_feature,   0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->Add(m_tb_sketch,    0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->Add(m_tb_constrain, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 5);
    tbrow->AddStretchSpacer();
    m_toolbar->SetSizer(tbrow);

    // --- Sketch dialog (shape definition only — no distance/mode) ---
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

    // --- Extrude dialog (consumes the selected sketch) ---
    m_box_extrude = new wxBoxSizer(wxVERTICAL);
    m_extrude_sketch_label = new wxStaticText(m_form, wxID_ANY, _L("Sketch: —"));
    m_box_extrude->Add(m_extrude_sketch_label, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    {
        auto* eform = new wxFlexGridSizer(2, 6, 8);

        m_distance = make_spin(m_form, 10);
        eform->Add(new wxStaticText(m_form, wxID_ANY, _L("Extrude dist")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_distance);

        m_mode = new wxChoice(m_form, wxID_ANY);
        m_mode->Append("New");
        m_mode->Append("Add");
        m_mode->Append("Cut");
        m_mode->SetSelection(0);
        eform->Add(new wxStaticText(m_form, wxID_ANY, _L("Mode")), 0, wxALIGN_CENTER_VERTICAL);
        eform->Add(m_mode);

        m_box_extrude->Add(eform, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    }
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_tool(); });
        m_confirm_btns.push_back(ok);
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_tool(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_extrude->Add(row, 0, wxALL, 12);
    }
    root->Add(m_box_extrude, 0, wxEXPAND);

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
    root->Show(m_box_extrude, false, true);
    root->Show(m_box_dressup, false, true);
    root->Show(m_box_hole,    false, true);
    root->Show(m_box_thread,  false, true);

    m_form->FitInside();
    m_form->SetScrollRate(10, 10);
    m_form->SetMinSize(wxSize(264, -1));

    // Right column: a small view toolbar over the live 3D viewport that mirrors
    // the CadDocument body.
    m_viewport = new DesignCanvas(this);

    m_viewport->set_on_sketch_commit([this](const SketchProfile& prof, const SketchPlane& plane) {
        m_feature_counter++;
        m_doc.add_sketch_profile(prof, plane, "Sketch" + std::to_string(m_feature_counter));
        m_doc.recompute();
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(_L("Sketch created — select it and Extrude"));
        refresh_tree();
    });

    m_viewport->set_on_sketch_entities_commit(
        [this](const std::vector<SketchEntity>& ents, const SketchPlane& plane) {
            if (ents.empty()) {
                m_status->SetForegroundColour(wxColour(235, 110, 110));
                m_status->SetLabel(_L("Sketch empty — nothing committed"));
                m_status->Refresh();
                return;
            }
            m_feature_counter++;
            m_doc.add_sketch_entities(ents, plane, "Sketch" + std::to_string(m_feature_counter));
            m_doc.recompute();
            m_status->SetForegroundColour(wxNullColour);
            m_status->SetLabel(_L("Sketch created — select it and Extrude"));
            refresh_tree();
        });

    auto* vcol = new wxBoxSizer(wxVERTICAL);
    auto* vbar = new wxBoxSizer(wxHORIZONTAL);
    auto* b_fit = new wxButton(this, wxID_ANY, _L("⊹ Fit view"));
    b_fit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_viewport->fit_view(); });
    vbar->Add(b_fit, 0, wxRIGHT, 8);
    // Standard-view shortcuts (a minimal view-cube): snap the camera to a named
    // orientation, reusing Camera::select_view via DesignCanvas::set_view.
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

    // Onshape layout: top toolbar over [ slim left column | center viewport ].
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(m_form, 0, wxEXPAND);
    body->Add(vcol,   1, wxEXPAND);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_toolbar, 0, wxEXPAND);
    outer->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND);
    outer->Add(body, 1, wxEXPAND);
    SetSizer(outer);

    set_ui_mode(UiMode::Feature);
}

void DesignPanel::set_ui_mode(UiMode m)
{
    m_ui_mode = m;
    wxSizer* s = m_toolbar->GetSizer();
    s->Show(m_tb_feature,   m == UiMode::Feature,   true);
    s->Show(m_tb_sketch,    m == UiMode::Sketch,    true);
    s->Show(m_tb_constrain, m == UiMode::Constrain, true);
    m_toolbar->Layout();
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

void DesignPanel::on_add_sketch()
{
    SketchShape shape = (m_shape->GetSelection() == 1) ? SketchShape::Circle
                                                        : SketchShape::Rectangle;
    SketchPlane plane = plane_from_index(m_plane->GetSelection());
    m_feature_counter++;
    m_doc.add_sketch(shape, plane, m_width->GetValue(), m_height->GetValue(),
                     m_radius->GetValue(), "Sketch" + std::to_string(m_feature_counter));
    m_doc.recompute();  // a lone sketch yields an empty body; that is expected
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Sketch added — select it and Extrude"));
    refresh_tree();
}

void DesignPanel::on_add_extrude()
{
    BooleanMode mode = static_cast<BooleanMode>(m_mode->GetSelection());
    m_feature_counter++;
    m_doc.add_extrude(m_extrude_sketch_ref, m_distance->GetValue(), false, mode,
                      "Extrude" + std::to_string(m_feature_counter));
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

void DesignPanel::on_begin_constrain()
{
    int sel = m_tree->GetSelection();
    if (sel == wxNOT_FOUND || sel >= int(m_doc.features.size())) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Select a sketch in the tree first"));
        m_status->Refresh();
        return;
    }
    CadFeature& f = m_doc.features[sel];
    if (f.type != CadFeatureType::Sketch) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Selected feature is not a sketch"));
        m_status->Refresh();
        return;
    }

    // Entity sketches (Fase 4.2): pick Line entities; constraints solve against
    // entity endpoints in the kernel.
    if (!f.entities.empty()) {
        m_constrain_feat = sel;
        if (m_viewport) m_viewport->begin_constrain_entities(f.entities, f.plane);
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(_L("Pick 1-2 lines, then a constraint; right-click exits"));
        m_status->Refresh();
        return;
    }

    // Legacy profile path (Fase 3).
    if (f.profile.points.size() < 3) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Selected feature is not a sketch"));
        m_status->Refresh();
        return;
    }
    m_constrain_feat = sel;
    // Anchor the first profile point so H/V constraints don't let the sketch
    // float freely; fix_point captures the point's current position in the solver.
    if (f.constraints.empty())
        f.constraints.push_back(SketchConstraintDef{SketchConstraintType::Fix, 0, -1, -1, -1, 0.0});
    if (m_viewport) m_viewport->begin_constrain(f.profile, f.plane);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Pick 1-2 entities, then a constraint or dimension; right-click exits"));
    m_status->Refresh();
}

void DesignPanel::apply_entity_constraint(SketchConstraintType type)
{
    using R = SketchPointRole;
    using T = SketchConstraintType;
    int e0 = -1, e1 = -1;
    m_viewport->selected_constrain_entities(e0, e1);

    auto fail = [this](const wxString& msg) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(msg);
        m_status->Refresh();
    };
    // Onshape-style dimension entry: Button -> dialog -> confirm. Returns false on cancel.
    auto prompt_dimension = [this](const wxString& label, double& value) -> bool {
        const wxString preset = wxString::Format(wxT("%g"), value);
        const wxString s = wxGetTextFromUser(label, _L("Dimension"), preset, this);
        double parsed = 0.0;
        if (s.empty() || !s.ToDouble(&parsed)) return false;   // cancelled / invalid
        value = parsed;
        return true;
    };

    CadFeature& feat = m_doc.features[m_constrain_feat];
    const bool needs_two = (type == T::Parallel || type == T::Perpendicular ||
                            type == T::EqualLength || type == T::Coincident ||
                            type == T::Concentric || type == T::Tangent ||
                            type == T::Angle || type == T::Midpoint);
    if (e0 < 0 || e0 >= int(feat.entities.size()) ||
        (needs_two && (e1 < 0 || e1 >= int(feat.entities.size())))) {
        fail(needs_two ? _L("Pick two entities first") : _L("Pick an entity first"));
        return;
    }
    auto is_round = [](const SketchEntity& e) {
        return e.type == SketchEntity::Type::Circle || e.type == SketchEntity::Type::Arc; };

    SketchEntityConstraintDef def;
    def.type  = type;
    def.value = 0.0;
    switch (type) {
    case T::Horizontal:
    case T::Vertical:
        // One line: level/plumb its own two endpoints.
        def.ea = e0; def.ra = R::P0;
        def.eb = e0; def.rb = R::P1;
        break;
    case T::Parallel:
    case T::Perpendicular:
    case T::EqualLength:
        def.ea = e0; def.eb = e1;   // two whole line segments (roles unused)
        break;
    case T::Coincident: {
        // Join the closest endpoint pair of the two picked lines.
        const SketchEntity& A = feat.entities[e0];
        const SketchEntity& B = feat.entities[e1];
        const std::pair<R, Vec2d> aps[2] = {{R::P0, A.p0}, {R::P1, A.p1}};
        const std::pair<R, Vec2d> bps[2] = {{R::P0, B.p0}, {R::P1, B.p1}};
        R ra = R::P1, rb = R::P0;
        double best = 1e30;
        for (const auto& ap : aps)
            for (const auto& bp : bps) {
                const double d = (ap.second - bp.second).squaredNorm();
                if (d < best) { best = d; ra = ap.first; rb = bp.first; }
            }
        def.ea = e0; def.ra = ra; def.eb = e1; def.rb = rb;
        break;
    }
    case T::Concentric: {
        // Two circles/arcs: make their centres coincide.
        if (!is_round(feat.entities[e0]) || !is_round(feat.entities[e1])) {
            fail(_L("Concentric needs two circles or arcs")); return;
        }
        def.ea = e0; def.ra = R::Center; def.eb = e1; def.rb = R::Center;
        break;
    }
    case T::Tangent: {
        // line+round or round+round; the kernel detects the entity types.
        const bool ok = (is_round(feat.entities[e0]) && feat.entities[e1].type == SketchEntity::Type::Line) ||
                        (is_round(feat.entities[e1]) && feat.entities[e0].type == SketchEntity::Type::Line) ||
                        (is_round(feat.entities[e0]) && is_round(feat.entities[e1]));
        if (!ok) { fail(_L("Tangent needs a line and a circle/arc, or two circles/arcs")); return; }
        def.ea = e0; def.eb = e1;
        break;
    }
    case T::Angle: {
        // Angle between two line segments; value entered in degrees.
        double deg = 90.0;
        if (!prompt_dimension(_L("Angle (degrees)"), deg)) return;
        def.ea = e0; def.eb = e1;
        def.value = deg * M_PI / 180.0;
        break;
    }
    case T::Midpoint: {
        // One pick is a Point, the other a Line: the point is the line's midpoint.
        const SketchEntity& A = feat.entities[e0];
        const SketchEntity& B = feat.entities[e1];
        int pt = -1, ln = -1;
        if (A.type == SketchEntity::Type::Point && B.type == SketchEntity::Type::Line) { pt = e0; ln = e1; }
        else if (B.type == SketchEntity::Type::Point && A.type == SketchEntity::Type::Line) { pt = e1; ln = e0; }
        else { fail(_L("Midpoint needs a point and a line")); return; }
        def.ea = pt; def.ra = R::P0; def.eb = ln;
        break;
    }
    case T::Radius:
    case T::Diameter: {
        const SketchEntity& A = feat.entities[e0];
        if (!is_round(A)) { fail(_L("Radius/Diameter needs a circle or arc")); return; }
        double v = (type == T::Diameter) ? 2.0 * A.radius : A.radius;
        if (!prompt_dimension(type == T::Diameter ? _L("Diameter") : _L("Radius"), v)) return;
        if (v <= 0.0) { fail(_L("Dimension must be positive")); return; }
        def.ea = e0; def.ra = R::Center; def.value = v;
        break;
    }
    default:
        fail(_L("Unsupported constraint"));
        return;
    }

    // solve_sketch_feature rewrites entity coords even on failure, so snapshot
    // to roll back a rejected (over-constrained) addition cleanly.
    const std::vector<SketchEntity> saved = feat.entities;
    feat.entity_constraints.push_back(def);
    if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
        feat.entity_constraints.pop_back();
        feat.entities = saved;
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Constraint rejected (over-constrained)"));
        m_status->Refresh();
        return;
    }
    m_doc.recompute();
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        m_viewport->set_mesh(m_doc.display_mesh);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Applied constraint"));
    m_status->Refresh();
}

void DesignPanel::apply_edit_op(EditOp op)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport ||
        !m_viewport->is_constraining_entities()) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Press Constrain on a sketch first"));
        m_status->Refresh();
        return;
    }
    auto fail = [this](const wxString& msg) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(msg);
        m_status->Refresh();
    };
    // Onshape-style value entry: Button -> dialog -> confirm. Returns false on cancel.
    auto prompt_value = [this](const wxString& label, double& value) -> bool {
        const wxString preset = wxString::Format(wxT("%g"), value);
        const wxString s = wxGetTextFromUser(label, _L("Value"), preset, this);
        double parsed = 0.0;
        if (s.empty() || !s.ToDouble(&parsed)) return false;
        value = parsed;
        return true;
    };

    int e0 = -1, e1 = -1;
    m_viewport->selected_constrain_entities(e0, e1);
    CadFeature& feat = m_doc.features[m_constrain_feat];
    const int n = int(feat.entities.size());
    if (e0 < 0 || e0 >= n) { fail(_L("Pick an entity first")); return; }
    using Type = SketchEntity::Type;

    switch (op) {
    case EditOp::Mirror: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick the entity, then a mirror-axis line")); return; }
        const SketchEntity& axis = feat.entities[e1];
        if (axis.type != Type::Line) { fail(_L("Mirror axis must be a line")); return; }
        auto out = SketchEngine::mirror_entities({ feat.entities[e0] }, axis.p0, axis.p1);
        if (out.empty()) { fail(_L("Mirror produced nothing")); return; }
        for (auto& m : out) feat.entities.push_back(m);
        break;
    }
    case EditOp::Offset: {
        double d = 1.0;
        if (!prompt_value(_L("Offset distance (+left / -right of direction)"), d)) return;
        auto out = SketchEngine::offset_entities({ feat.entities[e0] }, d);
        if (out.empty()) { fail(_L("Offset collapsed the entity")); return; }
        for (auto& o : out) feat.entities.push_back(o);
        break;
    }
    case EditOp::Fillet: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick two lines to fillet")); return; }
        if (feat.entities[e0].type != Type::Line || feat.entities[e1].type != Type::Line) {
            fail(_L("Fillet needs two lines")); return;
        }
        double r = 1.0;
        if (!prompt_value(_L("Fillet radius"), r)) return;
        if (r <= 0.0) { fail(_L("Radius must be positive")); return; }
        SketchEntity a_out, b_out, arc_out;
        if (!SketchEngine::fillet_lines(feat.entities[e0], feat.entities[e1], r, a_out, b_out, arc_out)) {
            fail(_L("Fillet failed (parallel lines or radius too large)")); return;
        }
        feat.entities[e0] = a_out;
        feat.entities[e1] = b_out;
        feat.entities.push_back(arc_out);
        break;
    }
    case EditOp::Trim:
    case EditOp::Extend: {
        if (feat.entities[e0].type != Type::Line) { fail(_L("Trim/Extend works on lines")); return; }
        Vec2d pick;
        if (!m_viewport->pick0_point(pick)) { fail(_L("Pick the line to trim/extend")); return; }
        std::vector<SketchEntity> others;
        others.reserve(n > 0 ? n - 1 : 0);
        for (int i = 0; i < n; ++i)
            if (i != e0) others.push_back(feat.entities[i]);
        const bool ok = (op == EditOp::Trim)
            ? SketchEngine::trim_entity(feat.entities[e0], others, pick)
            : SketchEngine::extend_entity(feat.entities[e0], others, pick);
        if (!ok) { fail(op == EditOp::Trim ? _L("Nothing to trim at the pick")
                                           : _L("No edge to extend to")); return; }
        break;
    }
    }

    m_doc.recompute();
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        m_viewport->set_mesh(m_doc.display_mesh);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Applied edit"));
    m_status->Refresh();
}

void DesignPanel::apply_constraint(SketchConstraintType type)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) ||
        m_viewport == nullptr) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Press Constrain on a sketch first"));
        m_status->Refresh();
        return;
    }

    // Entity sketches (Fase 4.2) route through the entity-constraint path.
    if (m_viewport->is_constraining_entities()) {
        apply_entity_constraint(type);
        return;
    }

    if (!m_viewport->is_constraining()) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Press Constrain on a sketch first"));
        m_status->Refresh();
        return;
    }
    int a = -1, b = -1;
    if (!m_viewport->selected_segment(a, b)) {
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Pick a segment in the viewport first"));
        m_status->Refresh();
        return;
    }
    CadFeature& feat = m_doc.features[m_constrain_feat];
    // solve_sketch_feature rewrites profile.points even on failure, so snapshot
    // the geometry to roll back a rejected constraint cleanly.
    const std::vector<Vec2d> saved_pts = feat.profile.points;
    feat.constraints.push_back(SketchConstraintDef{type, a, b, -1, -1, 0.0});
    if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
        feat.constraints.pop_back();        // reject the non-converging addition
        feat.profile.points = saved_pts;    // and restore the pre-solve geometry
        m_status->SetForegroundColour(wxColour(235, 110, 110));
        m_status->SetLabel(_L("Constraint rejected (over-constrained)"));
        m_status->Refresh();
        return;
    }
    m_doc.recompute();
    m_viewport->update_constrain_profile(m_doc.features[m_constrain_feat].profile.points);
    if (!m_doc.display_mesh.its.indices.empty())
        m_viewport->set_mesh(m_doc.display_mesh);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(type == SketchConstraintType::Horizontal ? _L("Applied Horizontal")
                                                                : _L("Applied Vertical"));
    m_status->Refresh();
}

void DesignPanel::reset_edit_state()
{
    m_edit_index = -1;
}

int DesignPanel::resolve_extrude_sketch() const
{
    int sel = m_tree->GetSelection();
    if (sel != wxNOT_FOUND && sel < int(m_doc.features.size()) &&
        m_doc.features[sel].type == CadFeatureType::Sketch)
        return sel;
    for (int i = int(m_doc.features.size()) - 1; i >= 0; --i)
        if (m_doc.features[i].type == CadFeatureType::Sketch) return i;
    return -1;
}

void DesignPanel::load_feature_into_dialog(const CadFeature& f)
{
    switch (f.type) {
    case CadFeatureType::Sketch:
        m_shape->SetSelection(f.shape == SketchShape::Circle ? 1 : 0);
        m_plane->SetSelection(index_from_plane(f.plane));
        m_width->SetValue(f.width);
        m_height->SetValue(f.height);
        m_radius->SetValue(f.radius);
        break;
    case CadFeatureType::Extrude:
        m_distance->SetValue(f.distance);
        m_mode->SetSelection(static_cast<int>(f.mode)); // New=0,Add=1,Cut=2
        m_extrude_sketch_ref = f.sketch_ref;
        if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size()))
            m_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_extrude_sketch_ref].name));
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
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Sketch);
        break;
    case CadFeatureType::Extrude:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Extrude);
        break;
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Dressup);
        break;
    case CadFeatureType::Hole:
        m_edit_index = sel;
        load_feature_into_dialog(f);
        open_tool(Tool::Hole);
        break;
    case CadFeatureType::Thread:
        m_edit_index = sel;
        load_feature_into_dialog(f);
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
        f.type   = CadFeatureType::Sketch;
        f.shape  = (m_shape->GetSelection() == 0) ? SketchShape::Rectangle : SketchShape::Circle;
        f.plane  = plane_from_index(m_plane->GetSelection());
        f.width  = m_width->GetValue();
        f.height = m_height->GetValue();
        f.radius = m_radius->GetValue();
        break;
    case Tool::Extrude:
        f.type       = CadFeatureType::Extrude;
        f.sketch_ref = m_extrude_sketch_ref;
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

    if (m_active == Tool::Sketch) {
        // A sketch carries no 3D solid; there is no ghost to show. It is always valid
        // for positive dims, so just enable Confirm and clear any stale ghost.
        m_viewport->clear_preview();
        m_status->SetForegroundColour(wxColour(120, 210, 120));
        m_status->SetLabel(_L("Sketch ready"));
        for (wxButton* b : m_confirm_btns) if (b) b->Enable(true);
        m_status->Refresh();
        return;
    }

    CadFeature   cand = build_candidate(m_active);
    TriangleMesh mesh;
    std::string  err;
    bool         ok = false;

    const bool editing_single = (m_edit_index >= 0);
    if (editing_single) {
        // Edit-mode preview: stacking the candidate on top of the live body would
        // re-apply the feature being edited (fillet-on-fillet) — wrong, and a
        // source of OCCT failures. Instead evaluate the *replace* on a throwaway
        // copy so the ghost is the true post-edit body.
        CadDocument tmp = m_doc;
        ok = tmp.replace_feature(m_edit_index, cand);
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
    s->Show(m_box_extrude, t == Tool::Extrude, true);
    s->Show(m_box_dressup, t == Tool::Dressup, true);
    s->Show(m_box_hole,    t == Tool::Hole,    true);
    s->Show(m_box_thread,  t == Tool::Thread,  true);

    if (t == Tool::Extrude) {
        if (m_extrude_sketch_ref >= 0 && m_extrude_sketch_ref < int(m_doc.features.size()))
            m_extrude_sketch_label->SetLabel(_L("Sketch: ") +
                wxString::FromUTF8(m_doc.features[m_extrude_sketch_ref].name));
    }

    m_form->Layout();
    m_form->FitInside();
    refresh_preview();
}

void DesignPanel::close_tool()
{
    m_active = Tool::None;
    wxSizer* s = m_form->GetSizer();
    s->Show(m_box_sketch,  false, true);
    s->Show(m_box_extrude, false, true);
    s->Show(m_box_dressup, false, true);
    s->Show(m_box_hole,    false, true);
    s->Show(m_box_thread,  false, true);
    m_viewport->clear_preview();
    m_form->Layout();
    m_form->FitInside();
}

void DesignPanel::confirm_tool()
{
    const bool editing_single = (m_edit_index >= 0);

    if (editing_single) {
        // Edit mode: overwrite the existing feature instead of appending.
        CadFeature cand = build_candidate(m_active);
        bool ok = m_doc.replace_feature(m_edit_index, cand);
        reset_edit_state();
        close_tool();          // clears the preview ghost
        after_tree_edit(ok);   // refresh tree/viewport/status (or "Edit rejected")
        return;
    }

    switch (m_active) {
    case Tool::Sketch:  on_add_sketch();  break;
    case Tool::Extrude: on_add_extrude(); break;
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
