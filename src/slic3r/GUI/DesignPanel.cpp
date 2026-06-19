#include "DesignPanel.hpp"
#include "DesignCanvas.hpp"
#include "DesignSketchTool.hpp"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include <wx/font.h>

#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm>

#include "slic3r/GUI/wxExtensions.hpp"   // ScalableButton, create_scaled_bitmap
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

namespace Slic3r { namespace GUI {

// Format a value with the international ('.') decimal separator regardless of the
// app's LC_NUMERIC locale (wx sets it to the user locale at startup). snprintf may
// emit a comma, so normalise it.
static wxString en_format(double v, int digits = 2)
{
    char fmt[16];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", digits);
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, v);
    for (char* c = buf; *c; ++c) if (*c == ',') *c = '.';
    return wxString::FromUTF8(buf);
}
// Parse a user-typed value accepting either '.' or ',' as the decimal separator.
static bool en_parse(const wxString& text, double& out)
{
    wxString t(text);
    t.Replace(wxT(","), wxT("."));
    return t.ToCDouble(&out);
}

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

    const wxColour tb_bg(0x32, 0x32, 0x37);
    const wxColour tb_hover(0x45, 0x45, 0x4C);
    auto icon_btn = [this, tb_bg, tb_hover](const char* icon, const wxString& tip) {
        auto* b = new ScalableButton(m_toolbar, wxID_ANY, icon, "", wxSize(34, 34),
                                     wxDefaultPosition, wxBU_EXACTFIT | wxBORDER_NONE, false, 22);
        b->SetToolTip(tip);
        b->SetBackgroundColour(tb_bg);
        // Subtle hover affordance (no theme-native highlight on a borderless button).
        b->Bind(wxEVT_ENTER_WINDOW, [b, tb_hover](wxMouseEvent& e) {
            b->SetBackgroundColour(tb_hover); b->Refresh(); e.Skip(); });
        b->Bind(wxEVT_LEAVE_WINDOW, [b, tb_bg](wxMouseEvent& e) {
            b->SetBackgroundColour(tb_bg); b->Refresh(); e.Skip(); });
        return b;
    };
    // Small grey group caption (Onshape-style section hint) for each toolbar mode.
    auto caption = [this](const wxString& t) {
        auto* s = new wxStaticText(m_toolbar, wxID_ANY, t);
        wxFont f = s->GetFont();
        f.SetPointSize(f.GetPointSize() - 2);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        s->SetFont(f);
        s->SetForegroundColour(wxColour(0x80, 0x80, 0x88));
        return s;
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
    m_tb_feature->Add(caption(_L("FEATURES")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
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
    m_tb_sketch->Add(caption(_L("SKETCH")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    {
        // The plane/orientation choice lives in the docked Sketch card (Phase 3),
        // not in the toolbar; the toolbar carries only the drawing tools.
        auto skbtn = [&](const char* icon, DesignSketchTool::Mode mode,
                         const wxString& tip, const wxString& hint) {
            auto* b = icon_btn(icon, tip);
            b->Bind(wxEVT_BUTTON, [select_tool, mode, hint](wxCommandEvent&) { select_tool(mode, hint); });
            sadd(b);
        };
        skbtn("design_select",    DesignSketchTool::Mode::Select,           _L("Select"),
              _L("Click to select; Shift to add; double-click for a whole loop"));
        skbtn("design_dimension", DesignSketchTool::Mode::Dimension, _L("Dimension"),
              _L("Click 2 points or a line / circle / arc to place a dimension"));
        add_sep(m_tb_sketch);
        skbtn("design_line",      DesignSketchTool::Mode::Line,             _L("Line"),
              _L("Click start, then end — then set the exact length"));
        skbtn("design_polyline",  DesignSketchTool::Mode::Polyline,         _L("Polyline"),
              _L("Click points; click first / right-click to close the loop"));
        skbtn("design_rect",      DesignSketchTool::Mode::CornerRect,       _L("Corner rectangle"),
              _L("Click two opposite corners"));
        skbtn("design_crect",     DesignSketchTool::Mode::CenterRect,       _L("Center rectangle"),
              _L("Click center, then a corner"));
        skbtn("design_rect_oblique", DesignSketchTool::Mode::ObliqueRect,   _L("Oblique rectangle"),
              _L("Click two corners of one edge, then a point for the width"));
        skbtn("design_rect_rounded", DesignSketchTool::Mode::RoundedRect,   _L("Rounded rectangle"),
              _L("Click two opposite corners, then a point for the corner radius"));
        skbtn("design_circle",    DesignSketchTool::Mode::CenterCircle,     _L("Center circle"),
              _L("Click center, then radius"));
        skbtn("design_circle2pt", DesignSketchTool::Mode::TwoPointCircle,   _L("2-point circle"),
              _L("Click two ends of the diameter"));
        skbtn("design_point",     DesignSketchTool::Mode::Point,            _L("Point"),
              _L("Click to place a point"));
        skbtn("design_circle3pt", DesignSketchTool::Mode::ThreePointCircle, _L("3-point circle"),
              _L("Click three points on the circle"));
        skbtn("design_arc3pt",    DesignSketchTool::Mode::ThreePointArc,    _L("3-point arc"),
              _L("Click start, end, then a point on the arc"));
        skbtn("design_tangentarc", DesignSketchTool::Mode::TangentArc,      _L("Tangent arc"),
              _L("Click start (on the last entity) then end"));
        skbtn("design_arc_center", DesignSketchTool::Mode::CenterArc,       _L("Center-point arc"),
              _L("Click center, then start, then a point for the end angle"));
        skbtn("design_slot",      DesignSketchTool::Mode::Slot,             _L("Slot"),
              _L("Click two centerline ends, then a point for width"));
        skbtn("design_slot_arc",  DesignSketchTool::Mode::ArcSlot,          _L("Arc slot"),
              _L("Click center, start, end, then a point for the width"));
        skbtn("design_ellipse",   DesignSketchTool::Mode::Ellipse,          _L("Ellipse"),
              _L("Click center, a major-axis end, then a point for the minor axis"));
        skbtn("design_ellipse_arc", DesignSketchTool::Mode::EllipseArc,     _L("Elliptical arc"),
              _L("Click center, major-axis end, minor point, then arc start and end"));
        skbtn("design_bspline",   DesignSketchTool::Mode::BSpline,          _L("Spline"),
              _L("Click control points; double-click or right-click to finish"));

        m_sides = new wxSpinCtrl(m_toolbar, wxID_ANY, "6", wxDefaultPosition, wxSize(50, -1));
        m_sides->SetRange(3, 64);
        m_sides->SetValue(6);
        auto* b_poly = icon_btn("design_polygon", _L("Polygon"));
        b_poly->Bind(wxEVT_BUTTON, [this, select_tool](wxCommandEvent&) {
            if (m_viewport) {
                m_viewport->set_sketch_polygon_sides(m_sides->GetValue());
                m_viewport->set_sketch_polygon_circumscribed(m_poly_circ && m_poly_circ->GetValue());
            }
            select_tool(DesignSketchTool::Mode::Polygon, _L("Click center then a vertex")); });
        m_sides->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
            if (m_viewport) m_viewport->set_sketch_polygon_sides(m_sides->GetValue()); });
        sadd(b_poly);
        sadd(m_sides);
        m_poly_circ = new wxCheckBox(m_toolbar, wxID_ANY, _L("Circumscribed"));
        m_poly_circ->SetForegroundColour(wxColour(0xC8, 0xC8, 0xC8));
        m_poly_circ->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (m_viewport) m_viewport->set_sketch_polygon_circumscribed(m_poly_circ->GetValue()); });
        sadd(m_poly_circ);
        add_sep(m_tb_sketch);
        m_construction->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (m_viewport && m_viewport->is_sketching())
                m_viewport->set_sketch_construction(m_construction->GetValue()); });
        sadd(m_construction);
        add_sep(m_tb_sketch);
        auto* b_del = icon_btn("design_delete", _L("Delete selected"));
        b_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_viewport) m_viewport->delete_selected_sketch_entities(); });
        sadd(b_del);
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
    m_tb_constrain->Add(caption(_L("CONSTRAIN")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
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
        cbtn("design_c_symmetric",     _L("Symmetric"),     SketchConstraintType::Symmetric);
        cbtn("design_c_angle",         _L("Angle"),         SketchConstraintType::Angle);
        cbtn("design_c_radius",        _L("Radius"),        SketchConstraintType::Radius);
        cbtn("design_c_diameter",      _L("Diameter"),      SketchConstraintType::Diameter);
        cbtn("design_c_fix",           _L("Fix point (anchor in place)"), SketchConstraintType::Fix);
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
            cancel_value();                              // drop any open value card
            if (m_viewport) m_viewport->end_constrain(); // clear viewport pick highlight
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

    // Onshape-style dialog-card header: feature icon + bold title. out receives
    // the title control so open_tool() can retitle it per feature.
    auto card_header = [this](const char* icon, const wxString& title, wxStaticText*& out) -> wxSizer* {
        auto* h  = new wxBoxSizer(wxHORIZONTAL);
        auto* ic = new wxStaticBitmap(m_form, wxID_ANY, create_scaled_bitmap(icon, m_form, 18));
        out = new wxStaticText(m_form, wxID_ANY, title);
        wxFont f = out->GetFont();
        f.SetPointSize(f.GetPointSize() + 1);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        out->SetFont(f);
        h->Add(ic,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        h->Add(out, 0, wxALIGN_CENTER_VERTICAL);
        return h;
    };

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
    m_box_sketch->Add(card_header("design_sketch", _L("Sketch"), m_hdr_sketch), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_sketch->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
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
    m_box_extrude->Add(card_header("design_extrude", _L("Extrude"), m_hdr_extrude), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_extrude->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
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
    m_box_dressup->Add(card_header("design_dressup", _L("Fillet / Chamfer"), m_hdr_dressup), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_dressup->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
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
    m_box_hole->Add(card_header("design_hole", _L("Hole"), m_hdr_hole), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_hole->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
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
    m_box_thread->Add(card_header("design_thread", _L("Thread"), m_hdr_thread), 0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_thread->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
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

    // --- Docked value-entry card (Onshape Button->Dialog->Confirm for dimensions) ---
    m_box_value = new wxBoxSizer(wxVERTICAL);
    {
        // Header title doubles as the operation label (set by request_value()).
        m_box_value->Add(card_header("design_constrain", _L("Value"), m_value_label), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        m_box_value->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
        auto* vrow = new wxBoxSizer(wxHORIZONTAL);
        // wxTE_PROCESS_ENTER so the user can just type a value and press Enter to
        // apply it (the natural CAD-dimension gesture), not only click Confirm.
        // Plain text field (not a spin control): on wxGTK the native GtkSpinButton
        // formats per the user locale (comma) with no clean override, so we own the
        // formatting here to guarantee international '.' decimals.
        m_value_input = new wxTextCtrl(m_form, wxID_ANY, "", wxDefaultPosition,
                                       wxSize(90, -1), wxTE_PROCESS_ENTER);
        m_value_input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { confirm_value(); });
        vrow->Add(new wxStaticText(m_form, wxID_ANY, _L("Value")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        vrow->Add(m_value_input, 0, wxALIGN_CENTER_VERTICAL);
        m_box_value->Add(vrow, 0, wxLEFT | wxRIGHT | wxTOP, 12);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* ok  = new wxButton(m_form, wxID_ANY, _L("✓ Confirm"));
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm_value(); });
        auto* no  = new wxButton(m_form, wxID_ANY, _L("✗ Cancel"));
        no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { cancel_value(); });
        row->Add(ok, 0, wxRIGHT, 8);
        row->Add(no, 0);
        m_box_value->Add(row, 0, wxALL, 12);
    }
    root->Add(m_box_value, 0, wxEXPAND);

    // --- Sketch-entry card (Phase 3): plane/orientation, opens on "New sketch",
    //     persists until Finish. The toolbar holds only the drawing tools. ---
    m_box_sketch_session = new wxBoxSizer(wxVERTICAL);
    m_box_sketch_session->Add(card_header("design_sketch", _L("Sketch"), m_hdr_sketch_session),
                              0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_sketch_session->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
    {
        auto* prow = new wxBoxSizer(wxHORIZONTAL);
        prow->Add(new wxStaticText(m_form, wxID_ANY, _L("Plane")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        m_draw_plane = new wxChoice(m_form, wxID_ANY);
        m_draw_plane->Append("XY"); m_draw_plane->Append("XZ"); m_draw_plane->Append("YZ");
        m_draw_plane->SetSelection(0);
        prow->Add(m_draw_plane, 0, wxALIGN_CENTER_VERTICAL);
        m_box_sketch_session->Add(prow, 0, wxLEFT | wxRIGHT | wxTOP, 12);
        auto* hint = new wxStaticText(m_form, wxID_ANY,
            _L("Pick a plane, then draw. Finish (✓) when done."));
        hint->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
        m_box_sketch_session->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
    }
    root->Add(m_box_sketch_session, 0, wxEXPAND);

    // --- Constraint-manager card (C3.4): list of the constrained sketch's
    //     entity-constraints; each row selects (highlights) + deletes. Shown only
    //     in Constrain mode; rebuilt by rebuild_constraint_list().
    m_box_constraints = new wxBoxSizer(wxVERTICAL);
    m_box_constraints->Add(card_header("design_constrain", _L("Constraints"), m_hdr_constraints),
                           0, wxLEFT | wxRIGHT | wxTOP, 12);
    m_box_constraints->Add(new wxStaticLine(m_form), 0, wxEXPAND | wxALL, 8);
    m_constraint_rows = new wxBoxSizer(wxVERTICAL);
    m_box_constraints->Add(m_constraint_rows, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    root->Add(m_box_constraints, 0, wxEXPAND);

    root->Add(new wxStaticText(m_form, wxID_ANY, _L("Feature tree")), 0, wxLEFT | wxTOP, 12);
    m_tree = new wxTreeCtrl(m_form, wxID_ANY, wxDefaultPosition, wxSize(-1, 140),
                            wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES |
                            wxTR_FULL_ROW_HIGHLIGHT | wxBORDER_SIMPLE);
    // Per-feature-type icons (indices match tree_icon_for): sketch/extrude/dressup/hole/thread.
    m_tree_images = new wxImageList(16, 16);
    m_tree_images->Add(create_scaled_bitmap("design_sketch",  nullptr, 16)); // 0 Sketch
    m_tree_images->Add(create_scaled_bitmap("design_extrude", nullptr, 16)); // 1 Extrude
    m_tree_images->Add(create_scaled_bitmap("design_dressup", nullptr, 16)); // 2 Fillet/Chamfer
    m_tree_images->Add(create_scaled_bitmap("design_hole",    nullptr, 16)); // 3 Hole
    m_tree_images->Add(create_scaled_bitmap("design_thread",  nullptr, 16)); // 4 Thread
    m_tree->AssignImageList(m_tree_images);
    root->Add(m_tree, 0, wxEXPAND | wxALL, 12);

    // Selecting a body-producing feature (Extrude/Fillet/Chamfer/Hole/Thread) in the
    // tree highlights the solid in the viewport; a Sketch row clears the highlight
    // (its face is already shown via the persistent sketch overlay).
    m_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) {
        if (!m_viewport) return;
        const int sel = tree_selection();
        const bool body = (sel >= 0 && sel < int(m_doc.features.size()) &&
                           m_doc.features[sel].type != CadFeatureType::Sketch &&
                           !m_doc.body.IsNull());
        m_viewport->set_body_highlight(body);
    });

    // Feature-tree edit row: act on the selected feature (delete / reorder).
    {
        auto* trow = new wxBoxSizer(wxHORIZONTAL);
        auto edit_btn = [this](const char* icon, const wxString& tip) {
            auto* b = new ScalableButton(m_form, wxID_ANY, icon, "", wxSize(30, 30),
                                         wxDefaultPosition, wxBU_EXACTFIT | wxBORDER_NONE, false, 20);
            b->SetToolTip(tip);
            return b;
        };
        auto* edit = edit_btn("design_edit", _L("Edit"));
        edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_edit_feature(); });
        auto* del  = edit_btn("design_delete", _L("Delete"));
        del->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_delete_feature(); });
        auto* up   = edit_btn("design_moveup", _L("Move up"));
        up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(-1); });
        auto* down = edit_btn("design_movedown", _L("Move down"));
        down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_move_feature(+1); });
        trow->Add(edit, 0, wxRIGHT, 4);
        trow->Add(del,  0, wxRIGHT, 4);
        trow->Add(up,   0, wxRIGHT, 4);
        trow->Add(down, 0);
        root->Add(trow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    }

    m_status = new wxStaticText(m_form, wxID_ANY, "");
    root->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // DoF / constraint-state readout (P3). Dedicated line so it never clobbers the
    // tool hint in m_status; updated by the on_solve_state callback after each solve.
    m_dof_status = new wxStaticText(m_form, wxID_ANY, "");
    {
        wxFont f = m_dof_status->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        m_dof_status->SetFont(f);
    }
    root->Add(m_dof_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

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
    root->Show(m_box_value,   false, true);
    root->Show(m_box_sketch_session, false, true);
    root->Show(m_box_constraints,    false, true);

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
        [this](const std::vector<SketchEntity>& ents,
               const std::vector<SketchEntityConstraintDef>& cons,
               const SketchPlane& plane) {
            if (ents.empty()) {
                m_status->SetForegroundColour(wxColour(235, 110, 110));
                m_status->SetLabel(_L("Sketch empty — nothing committed"));
                m_status->Refresh();
                return;
            }
            m_feature_counter++;
            const int sk = m_doc.add_sketch_entities(ents, plane,
                               "Sketch" + std::to_string(m_feature_counter), cons);
            if (!cons.empty()) m_doc.solve_sketch_feature(sk);   // enforce driving dimensions
            m_doc.recompute();
            m_status->SetForegroundColour(wxNullColour);
            m_status->SetLabel(cons.empty()
                ? _L("Sketch created — select it and Extrude")
                : wxString::Format(_L("Sketch created (%zu driving dims) — select it and Extrude"),
                                   cons.size()));
            refresh_tree();
            sync_sketch_display();   // keep the just-committed sketch visible as a face
        });

    // Live length/angle readout while drawing a Line/Polyline segment.
    m_viewport->set_on_cursor_metrics([this](double len, double ang_deg, bool locked) {
        double a = ang_deg; if (a < 0.0) a += 360.0;   // show bearing 0..360
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(wxString::Format(L"L %.2f mm   %.1f°%s",
                                            len, a, locked ? L"  (locked)" : L""));
        m_status->Refresh();
    });

    // DoF feedback (P3): after each live solve, report constraint state on its own
    // line. Green = fully constrained, red = conflicting, neutral = N remaining DoF.
    m_viewport->set_on_solve_state([this](int dof, bool ok, bool has_constraints) {
        if (!m_dof_status) return;
        if (!has_constraints) {
            m_dof_status->SetLabel(wxString());
        } else if (!ok) {
            m_dof_status->SetForegroundColour(wxColour(235, 80, 80));
            m_dof_status->SetLabel(_L("✗ Conflicting constraints"));
        } else if (dof == 0) {
            m_dof_status->SetForegroundColour(wxColour(80, 200, 110));
            m_dof_status->SetLabel(_L("✓ Fully constrained"));
        } else if (dof > 0) {
            m_dof_status->SetForegroundColour(wxColour(0xC8, 0xC8, 0xC8));
            m_dof_status->SetLabel(wxString::Format(_L("%d degrees of freedom"), dof));
        } else {
            m_dof_status->SetLabel(wxString());
        }
        m_dof_status->Refresh();
        m_form->Layout();
    });

    // Selection (Select tool): reflect the count in the status line.
    m_viewport->set_on_sketch_selection_changed([this](int count) {
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(count > 0
            ? wxString::Format(_L("%d selected — Delete removes them"), count)
            : _L("Click to select; click a filled face to extrude; Shift to add"));
        m_status->Refresh();
    });

    // Onshape flow: clicking inside a closed-loop face commits the sketch and opens
    // the Extrude dialog (with a ghost preview) targeting that sketch.
    m_viewport->set_on_sketch_face_selected([this]() {
        if (!m_viewport) return;
        m_viewport->finish_sketch();                 // commit live sketch (synchronous)
        m_extrude_sketch_ref = resolve_extrude_sketch();
        if (m_extrude_sketch_ref < 0) {
            m_status->SetForegroundColour(wxColour(235, 110, 110));
            m_status->SetLabel(_L("Could not resolve the sketch to extrude"));
            m_status->Refresh();
            return;
        }
        set_ui_mode(UiMode::Feature);
        open_tool(Tool::Extrude);
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(_L("Face selected — set the depth and Confirm"));
        m_status->Refresh();
    });

    // Line tool: after the segment is placed, ask for the exact length (Confirm
    // rescales it; Cancel keeps it as drawn).
    m_viewport->set_on_segment_drawn([this](double len, double /*ang_deg*/) {
        request_value(_L("Length (mm)"), len, 0.001, 1000000.0,
                      [this](double v) { if (m_viewport) m_viewport->apply_segment_length(v); },
                      [this]()         { if (m_viewport) m_viewport->keep_segment_as_drawn(); });
    });

    // Dimension tool: a click placed a quote at its measured value; pop the value card
    // pre-filled so the user can type an exact value (which drives the geometry).
    m_viewport->set_on_dimension_pick_complete([this](double current) {
        if (!m_viewport) return;
        using D = DesignSketchTool::DimType;
        const D k = m_viewport->pending_dimension_type();
        wxString label = _L("Value");
        double mn = 0.001, mx = 1000000.0;
        switch (k) {
        case D::Length:         label = _L("Length (mm)");   break;
        case D::Diameter:       label = _L("Diameter (mm)"); break;
        case D::Radius:         label = _L("Radius (mm)");   break;
        case D::Distance:       label = _L("Distance (mm) — 0 = coincident"); mn = 0.0; break;
        case D::DistanceToLine: label = _L("Distance to line (mm) — 0 = on the axis"); mn = 0.0; break;
        default: break;
        }
        request_value(label, current, mn, mx,
                      [this](double v) { if (m_viewport) m_viewport->set_sketch_dimension_value(v); },
                      [this]()         { if (m_viewport) m_viewport->cancel_sketch_dimension(); });
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
    // Phase 3: the docked Sketch card (plane/orientation) shows for the whole Sketch
    // session and hides on Finish/Constrain.
    if (m_box_sketch_session != nullptr && m_form != nullptr && m_form->GetSizer() != nullptr) {
        if (m == UiMode::Sketch && m_hdr_sketch_session != nullptr)
            m_hdr_sketch_session->SetLabel(wxString::Format(_L("Sketch %d"), m_feature_counter + 1));
        m_form->GetSizer()->Show(m_box_sketch_session, m == UiMode::Sketch, true);
        m_form->Layout();
        m_form->FitInside();
    }
    // Constraint-manager card follows Constrain mode; rebuilt from the active feature.
    if (m_box_constraints != nullptr && m_form != nullptr && m_form->GetSizer() != nullptr) {
        if (m == UiMode::Constrain)
            rebuild_constraint_list();
        else
            m_form->GetSizer()->Show(m_box_constraints, false, true);
        m_form->Layout();
        m_form->FitInside();
    }
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
    sync_sketch_display();
}

// Draw every committed sketch that no enabled Extrude consumes, so a sketch stays
// visible (as a translucent face + outline) when it is not part of the solid — e.g.
// after its Extrude is removed, or right after Finish.
void DesignPanel::sync_sketch_display()
{
    if (m_viewport == nullptr) return;
    const int n = int(m_doc.features.size());
    std::vector<bool> consumed(n, false);
    for (const CadFeature& f : m_doc.features)
        if (f.type == CadFeatureType::Extrude && f.enabled &&
            f.sketch_ref >= 0 && f.sketch_ref < n)
            consumed[f.sketch_ref] = true;

    std::vector<DesignSketchTool::DisplaySketch> ds;
    for (int i = 0; i < n; ++i) {
        const CadFeature& f = m_doc.features[i];
        if (f.type != CadFeatureType::Sketch || consumed[i] || f.entities.empty())
            continue;
        ds.push_back({ f.entities, f.plane });
    }
    m_viewport->set_display_sketches(std::move(ds));
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

int DesignPanel::tree_icon_for(CadFeatureType t)
{
    switch (t) {
    case CadFeatureType::Sketch:  return 0;
    case CadFeatureType::Extrude: return 1;
    case CadFeatureType::Fillet:
    case CadFeatureType::Chamfer: return 2;
    case CadFeatureType::Hole:    return 3;
    case CadFeatureType::Thread:  return 4;
    }
    return 0;
}

void DesignPanel::refresh_tree()
{
    m_tree->DeleteAllItems();
    m_tree_items.clear();
    wxTreeItemId root = m_tree->AddRoot("root");
    for (const auto& f : m_doc.features) {
        const int img = tree_icon_for(f.type);
        m_tree_items.push_back(
            m_tree->AppendItem(root, wxString::FromUTF8(f.name), img, img));
    }
}

int DesignPanel::tree_selection() const
{
    const wxTreeItemId sel = m_tree->GetSelection();
    if (!sel.IsOk()) return wxNOT_FOUND;
    for (size_t i = 0; i < m_tree_items.size(); ++i)
        if (m_tree_items[i] == sel) return int(i);
    return wxNOT_FOUND;
}

void DesignPanel::set_tree_selection(int row)
{
    if (row >= 0 && row < int(m_tree_items.size()))
        m_tree->SelectItem(m_tree_items[row]);
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
        sync_sketch_display();   // empty body: show any un-consumed committed sketch
        m_status->SetLabel(wxString());
    } else {
        set_status_ok();
    }
    m_status->Refresh();
}

void DesignPanel::on_delete_feature()
{
    int sel = tree_selection();
    if (sel == wxNOT_FOUND) {
        m_status->SetLabel(_L("Select a feature in the tree first"));
        m_status->Refresh();
        return;
    }
    after_tree_edit(m_doc.remove_feature(sel));
}

void DesignPanel::on_move_feature(int delta)
{
    int sel = tree_selection();
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
        set_tree_selection(target); // keep the moved feature selected
    } else {
        after_tree_edit(false);
    }
}

void DesignPanel::on_begin_constrain()
{
    int sel = tree_selection();
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

    CadFeature& feat = m_doc.features[m_constrain_feat];
    const bool needs_two = (type == T::Parallel || type == T::Perpendicular ||
                            type == T::EqualLength || type == T::Coincident ||
                            type == T::Concentric || type == T::Tangent ||
                            type == T::Angle || type == T::Midpoint ||
                            type == T::Symmetric);
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
        // Angle between two line segments; value entered (degrees) in the docked card.
        const int a = e0, b = e1;
        request_value(_L("Angle (degrees)"), 90.0, 0.0, 360.0, [this, a, b](double deg) {
            SketchEntityConstraintDef d;
            d.type = T::Angle; d.ea = a; d.eb = b;
            d.value = deg * M_PI / 180.0;
            commit_entity_constraint(d);
        });
        return;   // deferred: commit runs on Confirm
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
    case T::Symmetric: {
        // Two entities made symmetric about a third (axis) line. Picks: slot0=A,
        // slot1=B, slot2=axis. Two Points -> one pair; two Lines -> endpoint pairs.
        using ET = SketchEntity::Type;
        const int axis = m_viewport->selected_constrain_axis();
        if (axis < 0 || axis >= int(feat.entities.size()) ||
            feat.entities[axis].type != ET::Line) {
            fail(_L("Symmetric: pick two entities, then an axis line")); return;
        }
        const ET ta = feat.entities[e0].type, tb = feat.entities[e1].type;
        std::vector<SketchEntityConstraintDef> defs;
        auto mk = [&](R ra, R rb) {
            SketchEntityConstraintDef d;
            d.type = T::Symmetric;
            d.ea = e0; d.ra = ra; d.eb = e1; d.rb = rb; d.ec = axis;
            defs.push_back(d);
        };
        if (ta == ET::Point && tb == ET::Point) { mk(R::P0, R::P0); }
        else if (ta == ET::Line && tb == ET::Line) { mk(R::P0, R::P0); mk(R::P1, R::P1); }
        else { fail(_L("Symmetric needs two points or two lines + an axis")); return; }
        commit_entity_constraints(defs);
        return;   // multi-def commit done here
    }
    case T::Fix: {
        // Anchor the picked entity's reference point to its current coordinate (the
        // kernel pins it to a fixed reference). A single point — not both endpoints —
        // so it composes with any existing Horizontal/Vertical/length constraint
        // instead of duplicating it (pinning both endpoints of an already-horizontal
        // line is redundant → over-constrained). Removes 2 DoF (the entity's position);
        // combine with H/V + a dimension to reach fully constrained.
        using ET = SketchEntity::Type;
        const ET et = feat.entities[e0].type;
        def.ea = e0;
        def.ra = (et == ET::Circle || et == ET::Ellipse ||
                  et == ET::Arc    || et == ET::EllipseArc) ? R::Center : R::P0;
        break;
    }
    case T::Radius:
    case T::Diameter: {
        const SketchEntity& A = feat.entities[e0];
        if (!is_round(A)) { fail(_L("Radius/Diameter needs a circle or arc")); return; }
        const double cur = (type == T::Diameter) ? 2.0 * A.radius : A.radius;
        const int a = e0; const T tt = type;
        request_value(tt == T::Diameter ? _L("Diameter") : _L("Radius"), cur, 0.001, 100000.0,
            [this, a, tt](double v) {
                SketchEntityConstraintDef d;
                d.type = tt; d.ea = a; d.ra = R::Center; d.value = v;
                commit_entity_constraint(d);
            });
        return;   // deferred: commit runs on Confirm
    }
    default:
        fail(_L("Unsupported constraint"));
        return;
    }

    commit_entity_constraint(def);
}

void DesignPanel::commit_entity_constraint(const SketchEntityConstraintDef& def)
{
    commit_entity_constraints({ def });
}

void DesignPanel::commit_entity_constraints(const std::vector<SketchEntityConstraintDef>& defs)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) ||
        !m_viewport || defs.empty())
        return;
    CadFeature& feat = m_doc.features[m_constrain_feat];
    // solve_sketch_feature rewrites entity coords even on failure, so snapshot
    // to roll back a rejected (over-constrained) addition cleanly. Multiple defs
    // (Symmetric on two lines) must solve together, so push all then resize back.
    const std::vector<SketchEntity> saved = feat.entities;
    const size_t before = feat.entity_constraints.size();
    for (const auto& d : defs) feat.entity_constraints.push_back(d);
    if (!m_doc.solve_sketch_feature(m_constrain_feat)) {
        feat.entity_constraints.resize(before);
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

    refresh_constrain_dof();      // P3 DoF readout for the Constrain path
    rebuild_constraint_list();    // C3.4 manager: a row appeared
}

// Re-derive the solve state of the constrained feature and mirror it into the same
// DoF readout the in-session path uses, so "✓ Fully constrained" is reachable here.
void DesignPanel::refresh_constrain_dof()
{
    if (!m_dof_status || m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()))
        return;
    const CadFeature& feat = m_doc.features[m_constrain_feat];
    std::vector<SketchEntity> ents = feat.entities;   // already solved; re-solve is a cheap no-op
    const SketchSolveResult r = sketch_solve(ents, feat.entity_constraints);
    if (!r.ok) {
        m_dof_status->SetForegroundColour(wxColour(235, 80, 80));
        m_dof_status->SetLabel(_L("✗ Conflicting constraints"));
    } else if (r.dof == 0) {
        m_dof_status->SetForegroundColour(wxColour(80, 200, 110));
        m_dof_status->SetLabel(_L("✓ Fully constrained"));
    } else if (r.dof > 0) {
        m_dof_status->SetForegroundColour(wxColour(0xC8, 0xC8, 0xC8));
        m_dof_status->SetLabel(wxString::Format(_L("%d degrees of freedom"), r.dof));
    } else {
        m_dof_status->SetLabel(wxString());
    }
    m_dof_status->Refresh();
    m_form->Layout();
}

// Human-readable label for a constraint row, e.g. "Coincident L0·P1 — L1·P0",
// "Horizontal L2", "Radius C3 = 7.00". Entities are tagged by type letter + index.
wxString DesignPanel::constraint_label(const SketchEntityConstraintDef& d) const
{
    using T = SketchConstraintType;
    const CadFeature* feat = (m_constrain_feat >= 0 && m_constrain_feat < int(m_doc.features.size()))
                                 ? &m_doc.features[m_constrain_feat] : nullptr;
    auto tag = [&](int ei, SketchPointRole r) -> wxString {
        if (ei < 0) return wxString();
        char c = 'E';
        if (feat && ei < int(feat->entities.size())) {
            switch (feat->entities[ei].type) {
            case SketchEntity::Type::Line:       c = 'L'; break;
            case SketchEntity::Type::Circle:     c = 'C'; break;
            case SketchEntity::Type::Arc:        c = 'A'; break;
            case SketchEntity::Type::Point:      c = 'P'; break;
            case SketchEntity::Type::Ellipse:
            case SketchEntity::Type::EllipseArc: c = 'E'; break;
            case SketchEntity::Type::BSpline:    c = 'B'; break;
            }
        }
        wxString s; s << wxUniChar(c) << ei;   // avoid %c assert in Unicode build
        if (r == SketchPointRole::P1)     s += "·P1";
        else if (r == SketchPointRole::Center) s += "·Ctr";
        else if (r == SketchPointRole::P0)     s += "·P0";
        return s;
    };
    auto two = [&](const wxString& name) {
        return d.eb >= 0 ? wxString::Format("%s %s — %s", name, tag(d.ea, d.ra), tag(d.eb, d.rb))
                         : wxString::Format("%s %s", name, tag(d.ea, d.ra));
    };
    switch (d.type) {
    case T::Fix:           return wxString::Format(_L("Fix %s"), tag(d.ea, d.ra));
    case T::Coincident:    return two(_L("Coincident"));
    case T::Horizontal:    return two(_L("Horizontal"));
    case T::Vertical:      return two(_L("Vertical"));
    case T::Distance:      return wxString::Format("%s = %s", two(_L("Distance")), en_format(d.value));
    case T::LockX:         return wxString::Format(_L("Lock X %s"), tag(d.ea, d.ra));
    case T::LockY:         return wxString::Format(_L("Lock Y %s"), tag(d.ea, d.ra));
    case T::EqualLength:   return two(_L("Equal"));
    case T::Parallel:      return two(_L("Parallel"));
    case T::Perpendicular: return two(_L("Perpendicular"));
    case T::Concentric:    return two(_L("Concentric"));
    case T::Tangent:       return two(_L("Tangent"));
    case T::Midpoint:      return two(_L("Midpoint"));
    case T::Symmetric:     return wxString::Format(_L("Symmetric %s — %s / %s"),
                                                   tag(d.ea, d.ra), tag(d.eb, d.rb), tag(d.ec, d.rc));
    case T::Angle:         return wxString::Format("%s = %s°", two(_L("Angle")), en_format(d.value, 1));
    case T::Radius:        return wxString::Format("%s %s = %s", _L("Radius"),   tag(d.ea, d.ra), en_format(d.value));
    case T::Diameter:      return wxString::Format("%s %s = %s", _L("Diameter"), tag(d.ea, d.ra), en_format(d.value));
    case T::PointOnLine:   return two(_L("On line"));
    case T::PointOnObject: return two(_L("On edge"));
    }
    return _L("Constraint");
}

// Rebuild the constraint-row list from the constrained feature's entity_constraints.
void DesignPanel::rebuild_constraint_list()
{
    if (m_constraint_rows == nullptr || m_form == nullptr)
        return;
    m_constraint_rows->Clear(true /* delete windows */);
    m_constraint_sel = -1;

    const bool active = (m_constrain_feat >= 0 && m_constrain_feat < int(m_doc.features.size()));
    const std::vector<SketchEntityConstraintDef> empty;
    const std::vector<SketchEntityConstraintDef>& cons =
        active ? m_doc.features[m_constrain_feat].entity_constraints : empty;

    if (m_hdr_constraints)
        m_hdr_constraints->SetLabel(wxString::Format(_L("Constraints (%d)"), int(cons.size())));

    if (cons.empty()) {
        auto* none = new wxStaticText(m_form, wxID_ANY, _L("No constraints yet"));
        none->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
        m_constraint_rows->Add(none, 0, wxTOP, 4);
    }
    for (int i = 0; i < int(cons.size()); ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        // Delete button first (fixed left position, always visible — long labels can
        // horizontally scroll but ✗ stays put and clickable). BMP-safe ✗ glyph.
        auto* del = new wxButton(m_form, wxID_ANY, wxString::FromUTF8("✗"),
                                 wxDefaultPosition, wxSize(26, -1));
        del->SetToolTip(_L("Delete constraint"));
        del->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { delete_constraint(i); });
        // Clickable label: selecting it highlights the referenced entities.
        auto* lbl = new wxButton(m_form, wxID_ANY, constraint_label(cons[i]),
                                 wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBORDER_NONE);
        lbl->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { highlight_constraint_entities(i); });
        row->Add(del, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        row->Add(lbl, 1, wxALIGN_CENTER_VERTICAL);
        m_constraint_rows->Add(row, 0, wxEXPAND | wxTOP, 2);
    }

    // Feed the same list to the viewport for the on-sketch glyph badges (C3.4b).
    if (m_viewport)
        m_viewport->set_constraint_glyphs(cons);

    m_form->GetSizer()->Show(m_box_constraints, m_ui_mode == UiMode::Constrain, true);
    m_form->Layout();
    m_form->FitInside();
}

// Push the entities referenced by constraint `idx` to the viewport as a yellow
// highlight (toggle off if the same row is clicked again).
void DesignPanel::highlight_constraint_entities(int idx)
{
    if (!m_viewport || m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()))
        return;
    const auto& cons = m_doc.features[m_constrain_feat].entity_constraints;
    if (idx < 0 || idx >= int(cons.size()))
        return;
    if (m_constraint_sel == idx) {     // second click clears
        m_constraint_sel = -1;
        m_viewport->set_constraint_highlight({});
        return;
    }
    m_constraint_sel = idx;
    const SketchEntityConstraintDef& d = cons[idx];
    std::vector<int> ents;
    for (int e : { d.ea, d.eb, d.ec })
        if (e >= 0) ents.push_back(e);
    m_viewport->set_constraint_highlight(std::move(ents));
}

// Drop constraint `idx`, re-solve the feature, and refresh viewport + list + DoF.
void DesignPanel::delete_constraint(int idx)
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport)
        return;
    CadFeature& feat = m_doc.features[m_constrain_feat];
    if (idx < 0 || idx >= int(feat.entity_constraints.size()))
        return;
    feat.entity_constraints.erase(feat.entity_constraints.begin() + idx);
    // Re-solve the remaining system (deleting a constraint can only free DoF, so it
    // cannot fail for over-constraint; ignore the bool and refresh either way).
    m_doc.solve_sketch_feature(m_constrain_feat);
    m_doc.recompute();
    m_viewport->set_constraint_highlight({});
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        m_viewport->set_mesh(m_doc.display_mesh);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Constraint deleted"));
    m_status->Refresh();
    refresh_constrain_dof();
    rebuild_constraint_list();
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
        const int mi = n;   // index the single mirrored copy lands at (n entities before push)
        for (auto& m : out) feat.entities.push_back(m);

        // C4a: bind the mirror to its source with Symmetric constraints about the
        // axis, so the pair stays mirror-symmetric under later solves and drags.
        // mirror_entities preserves P0/P1/Center ordering, so the constraints are
        // satisfied by construction; if the solver still rejects them (degenerate
        // axis, redundancy) keep the geometry and drop only the binding.
        {
            using R  = SketchPointRole;
            using CT = SketchConstraintType;
            const std::vector<SketchEntity> saved_ents = feat.entities;
            const size_t cons_before = feat.entity_constraints.size();
            SketchEntityConstraintDef d; d.type = CT::Symmetric; d.ea = e0; d.eb = mi; d.ec = e1;
            const Type st = feat.entities[e0].type;
            if (st == Type::Line) {
                d.ra = R::P0; d.rb = R::P0; feat.entity_constraints.push_back(d);
                d.ra = R::P1; d.rb = R::P1; feat.entity_constraints.push_back(d);
            } else if (st == Type::Arc || st == Type::Circle) {
                d.ra = R::Center; d.rb = R::Center; feat.entity_constraints.push_back(d);
            } else if (st == Type::Point) {
                d.ra = R::P0; d.rb = R::P0; feat.entity_constraints.push_back(d);
            }
            if (feat.entity_constraints.size() != cons_before &&
                !m_doc.solve_sketch_feature(m_constrain_feat)) {
                feat.entity_constraints.resize(cons_before);
                feat.entities = saved_ents;
            }
        }
        break;
    }
    case EditOp::Offset: {
        const int a = e0;
        request_value(_L("Offset distance (+left / -right of direction)"), 1.0, -100000.0, 100000.0,
            [this, a](double d) {
                CadFeature& f = m_doc.features[m_constrain_feat];
                if (a >= int(f.entities.size())) return;
                auto out = SketchEngine::offset_entities({ f.entities[a] }, d);
                if (out.empty()) {
                    m_status->SetForegroundColour(wxColour(235, 110, 110));
                    m_status->SetLabel(_L("Offset collapsed the entity")); m_status->Refresh(); return;
                }
                const int ni = int(f.entities.size());   // offset copy lands here
                for (auto& o : out) f.entities.push_back(o);

                // C4c: bind the offset copy to its source. Offset only ADDS geometry
                // (the source is untouched), so unlike trim/fillet there are no stale
                // constraints to drop — just glue the pair. A line offset stays
                // Parallel to its source; an arc/circle offset stays Concentric (same
                // centre). Single constraint, so no degradation ladder; solve and roll
                // the binding back if the solver rejects it (keep the geometry).
                {
                    using CT = SketchConstraintType;
                    const Type st = f.entities[a].type;
                    SketchEntityConstraintDef d2; d2.ea = a; d2.eb = ni;
                    bool emit = true;
                    if (st == Type::Line)                              d2.type = CT::Parallel;
                    else if (st == Type::Arc || st == Type::Circle)    d2.type = CT::Concentric;
                    else                                               emit = false;
                    if (emit) {
                        const size_t cbefore = f.entity_constraints.size();
                        f.entity_constraints.push_back(d2);
                        if (!m_doc.solve_sketch_feature(m_constrain_feat))
                            f.entity_constraints.resize(cbefore);
                    }
                }
                after_edit_op();
            });
        return;   // deferred: edit runs on Confirm
    }
    case EditOp::Fillet: {
        if (e1 < 0 || e1 >= n) { fail(_L("Pick two lines to fillet")); return; }
        if (feat.entities[e0].type != Type::Line || feat.entities[e1].type != Type::Line) {
            fail(_L("Fillet needs two lines")); return;
        }
        const int a = e0, b = e1;
        request_value(_L("Fillet radius"), 1.0, 0.001, 100000.0, [this, a, b](double r) {
            CadFeature& f = m_doc.features[m_constrain_feat];
            if (a >= int(f.entities.size()) || b >= int(f.entities.size())) return;
            SketchEntity a_out, b_out, arc_out;
            if (!SketchEngine::fillet_lines(f.entities[a], f.entities[b], r, a_out, b_out, arc_out)) {
                m_status->SetForegroundColour(wxColour(235, 110, 110));
                m_status->SetLabel(_L("Fillet failed (parallel lines or radius too large)"));
                m_status->Refresh(); return;
            }
            f.entities[a] = a_out;
            f.entities[b] = b_out;
            const int arc = int(f.entities.size());
            f.entities.push_back(arc_out);

            // C4b: glue the fillet arc to the two trimmed lines so it survives a
            // re-solve instead of floating free. arc.p0 sits on line a's moved
            // endpoint, arc.p1 on line b's; recover the exact endpoint roles by
            // nearest match, then emit Coincident (essential — keeps the corner
            // joined) + Tangent (smoothness). The full set can be redundant for the
            // arc, so try it first and drop tangents progressively until the solver
            // accepts it; the Coincident pins survive even if tangency is rejected.
            {
                using R  = SketchPointRole;
                using CT = SketchConstraintType;
                auto role_near = [](const SketchEntity& ln, const Vec2d& p) -> R {
                    return ((ln.p0 - p).squaredNorm() <= (ln.p1 - p).squaredNorm()) ? R::P0 : R::P1;
                };
                const R ra = role_near(f.entities[a], arc_out.p0);
                const R rb = role_near(f.entities[b], arc_out.p1);

                // Fillet trims both lines back from the shared corner, so any
                // constraint anchored to a trimmed endpoint is now stale: the corner
                // Coincident that joined (a,ra)·(b,rb), and each line's own length
                // Distance (its length just changed). Drop them before re-binding —
                // leaving them would fight the new arc geometry and reject every
                // binding below.
                auto refs = [](const SketchEntityConstraintDef& d, int e, R r) {
                    return (d.ea == e && d.ra == r) || (d.eb == e && d.rb == r);
                };
                auto self_len = [](const SketchEntityConstraintDef& d, int e) {
                    return d.type == CT::Distance && d.ea == e && d.eb == e;
                };
                auto& cs = f.entity_constraints;
                cs.erase(std::remove_if(cs.begin(), cs.end(),
                    [&](const SketchEntityConstraintDef& d) {
                        return (d.type == CT::Coincident && refs(d, a, ra) && refs(d, b, rb))
                            || self_len(d, a) || self_len(d, b);
                    }), cs.end());

                auto coin = [&](R arc_role, int ln, R ln_role) {
                    SketchEntityConstraintDef d; d.type = CT::Coincident;
                    d.ea = arc; d.ra = arc_role; d.eb = ln; d.rb = ln_role; return d;
                };
                auto tang = [&](int ln) {
                    SketchEntityConstraintDef d; d.type = CT::Tangent; d.ea = arc; d.eb = ln; return d;
                };
                const std::vector<std::vector<SketchEntityConstraintDef>> ladder = {
                    { coin(R::P0, a, ra), coin(R::P1, b, rb), tang(a), tang(b) },
                    { coin(R::P0, a, ra), coin(R::P1, b, rb), tang(a) },
                    { coin(R::P0, a, ra), coin(R::P1, b, rb) },
                };
                const std::vector<SketchEntity> saved = f.entities;
                const size_t cbefore = f.entity_constraints.size();
                for (const auto& set : ladder) {
                    for (const auto& d : set) f.entity_constraints.push_back(d);
                    if (m_doc.solve_sketch_feature(m_constrain_feat)) break;   // accepted
                    f.entity_constraints.resize(cbefore);
                    f.entities = saved;
                }
            }
            after_edit_op();
        });
        return;   // deferred: edit runs on Confirm
    }
    case EditOp::Trim:
    case EditOp::Extend: {
        // Trim accepts Line/Arc/Circle subjects; Extend accepts Line/Arc (a Circle
        // is already closed, so there is nothing to extend).
        const Type st = feat.entities[e0].type;
        const bool subject_ok = (op == EditOp::Trim)
            ? (st == Type::Line || st == Type::Arc || st == Type::Circle)
            : (st == Type::Line || st == Type::Arc);
        if (!subject_ok) {
            fail(op == EditOp::Trim ? _L("Trim works on lines, arcs and circles")
                                    : _L("Extend works on lines and arcs"));
            return;
        }
        Vec2d pick;
        if (!m_viewport->pick0_point(pick)) { fail(_L("Pick the edge to trim/extend")); return; }
        std::vector<SketchEntity> others;
        others.reserve(n > 0 ? n - 1 : 0);
        for (int i = 0; i < n; ++i)
            if (i != e0) others.push_back(feat.entities[i]);
        const SketchEntity before = feat.entities[e0];   // C4.1: detect the moved endpoint
        const bool ok = (op == EditOp::Trim)
            ? SketchEngine::trim_entity(feat.entities[e0], others, pick)
            : SketchEngine::extend_entity(feat.entities[e0], others, pick);
        if (!ok) { fail(op == EditOp::Trim ? _L("Nothing to trim at the pick")
                                           : _L("No edge to extend to")); return; }

        // C4.1: trim/extend slides ONE endpoint of the subject along its own
        // direction (line) or sweep (arc). That (a) kills the subject's
        // self-length Distance dim and (b) detaches the moved endpoint from any
        // corner Coincident/PointOn* it used to hold. Drop both stale classes,
        // then re-anchor the moved endpoint onto the entity it now lands on with a
        // PointOnObject (the bridge picks PT_ON_LINE / PT_ON_CIRCLE). A Circle
        // subject restructures into an Arc (both endpoints new) — skip the
        // re-anchor there; its self constraints (Radius/Concentric) survive, so
        // there is nothing stale to drop either.
        if (st == Type::Line || st == Type::Arc) {
            using R  = SketchPointRole;
            using CT = SketchConstraintType;
            const SketchEntity& aft = feat.entities[e0];
            R     moved = R::P0;
            Vec2d P;
            if (st == Type::Line) {
                const bool p0_moved = (before.p0 - aft.p0).squaredNorm()
                                    > (before.p1 - aft.p1).squaredNorm();
                moved = p0_moved ? R::P0 : R::P1;
                P     = p0_moved ? aft.p0 : aft.p1;
            } else {
                const bool start_moved = std::abs(before.start_angle - aft.start_angle)
                                       > std::abs(before.end_angle - aft.end_angle);
                moved = start_moved ? R::P0 : R::P1;
                const double ang = start_moved ? aft.start_angle : aft.end_angle;
                P = aft.center + aft.radius * Vec2d(std::cos(ang), std::sin(ang));
            }

            // Drop stale: subject self-length Distance + any Coincident/PointOn*
            // pinning the moved endpoint to its old corner.
            auto refs = [&](const SketchEntityConstraintDef& d, R r) {
                return (d.ea == e0 && d.ra == r) || (d.eb == e0 && d.rb == r);
            };
            auto& cs = feat.entity_constraints;
            cs.erase(std::remove_if(cs.begin(), cs.end(),
                [&](const SketchEntityConstraintDef& d) {
                    if (d.type == CT::Distance && d.ea == e0 && d.eb == e0) return true;
                    return (d.type == CT::Coincident || d.type == CT::PointOnLine
                         || d.type == CT::PointOnObject) && refs(d, moved);
                }), cs.end());

            // Find which other entity the moved endpoint now lies on (Line/Circle
            // cutters only — PT_ON_* needs a line or circle primitive).
            int cutter = -1;
            const double tol = 1e-5;
            for (int i = 0; i < int(feat.entities.size()); ++i) {
                if (i == e0) continue;
                const SketchEntity& o = feat.entities[i];
                if (o.type == Type::Line) {
                    Vec2d dv = o.p1 - o.p0;
                    const double L2 = dv.dot(dv);
                    if (L2 < 1e-18) continue;
                    const double t = (P - o.p0).dot(dv) / L2;
                    if (t < -1e-6 || t > 1.0 + 1e-6) continue;
                    if ((o.p0 + t * dv - P).norm() < tol) { cutter = i; break; }
                } else if (o.type == Type::Circle) {
                    if (std::abs((P - o.center).norm() - o.radius) < tol) { cutter = i; break; }
                }
            }

            // Re-anchor with PointOnObject; keep geometry + the stale-drop even if
            // the solver rejects the new (possibly redundant) binding.
            if (cutter >= 0) {
                const size_t cbefore = feat.entity_constraints.size();
                SketchEntityConstraintDef d; d.type = CT::PointOnObject;
                d.ea = e0; d.ra = moved; d.eb = cutter;
                feat.entity_constraints.push_back(d);
                if (!m_doc.solve_sketch_feature(m_constrain_feat))
                    feat.entity_constraints.resize(cbefore);
            }
        }
        break;
    }
    }

    after_edit_op();
}

void DesignPanel::after_edit_op()
{
    if (m_constrain_feat < 0 || m_constrain_feat >= int(m_doc.features.size()) || !m_viewport)
        return;
    m_doc.recompute();
    m_viewport->set_constraint_highlight({});   // entity indices may have shifted
    m_viewport->update_constrain_entities(m_doc.features[m_constrain_feat].entities);
    if (!m_doc.display_mesh.its.indices.empty())
        m_viewport->set_mesh(m_doc.display_mesh);
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(_L("Applied edit"));
    m_status->Refresh();
    refresh_constrain_dof();
    rebuild_constraint_list();
}

void DesignPanel::request_value(const wxString& label, double def, double mn, double mx,
                                std::function<void(double)> cont,
                                std::function<void()> on_cancel)
{
    m_value_cont = std::move(cont);
    m_value_cancel = std::move(on_cancel);
    m_value_min = mn;
    m_value_max = mx;
    m_value_label->SetLabel(label);
    m_value_input->ChangeValue(en_format(def));   // '.' decimals, no EVT_TEXT feedback
    m_form->GetSizer()->Show(m_box_value, true, true);
    m_form->Layout();
    m_form->FitInside();
    m_value_input->SetFocus();
    m_value_input->SetSelection(-1, -1);   // select all so typing replaces the value
    m_status->SetForegroundColour(wxNullColour);
    m_status->SetLabel(label + _L(" — type a value, press Enter (or Confirm)"));
    m_status->Refresh();
}

void DesignPanel::confirm_value()
{
    if (!m_value_cont) { cancel_value(); return; }
    double v = 0.0;
    if (!en_parse(m_value_input->GetValue(), v)) { m_value_input->SetFocus(); return; }
    v = std::min(std::max(v, m_value_min), m_value_max);   // clamp to range
    auto cont = m_value_cont;            // copy, then clear before running so a
    m_value_cont = nullptr;              // re-entrant request_value can re-arm cleanly
    m_value_cancel = nullptr;            // confirmed: drop the cancel action
    m_form->GetSizer()->Show(m_box_value, false, true);
    m_form->Layout();
    m_form->FitInside();
    cont(v);                            // run the deferred constraint / edit-op apply
}

void DesignPanel::cancel_value()
{
    const bool was_open = (m_value_cont != nullptr);
    m_value_cont = nullptr;
    auto on_cancel = m_value_cancel;     // copy, clear, then run (re-entrancy safe)
    m_value_cancel = nullptr;
    if (m_box_value)
        m_form->GetSizer()->Show(m_box_value, false, true);
    m_form->Layout();
    m_form->FitInside();
    if (was_open) {
        m_status->SetForegroundColour(wxNullColour);
        m_status->SetLabel(wxString());
        m_status->Refresh();
    }
    if (on_cancel)
        on_cancel();                     // e.g. keep a pending line segment as drawn
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
    int sel = tree_selection();
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
    int sel = tree_selection();
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
    // A feature tool open with a live preview ghost (e.g. a fillet being previewed) is
    // NOT yet part of the body. Apply it first so "Commit to Plate" ships exactly what
    // is shown on screen, not the pre-feature solid. (confirm_tool() applies + closes.)
    if (m_active != Tool::None)
        confirm_tool();

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

    // Retitle the active card's header: edit-mode shows the feature's real name,
    // add-mode previews the type + next feature number (Onshape "Extrude 1").
    const bool editing = (m_edit_index >= 0 && m_edit_index < int(m_doc.features.size()));
    auto title = [&](const wxString& base) -> wxString {
        return editing ? wxString::FromUTF8(m_doc.features[m_edit_index].name)
                       : base + wxString::Format(" %d", m_feature_counter + 1);
    };
    switch (t) {
    case Tool::Sketch:  m_hdr_sketch->SetLabel(title(_L("Sketch")));   break;
    case Tool::Extrude: m_hdr_extrude->SetLabel(title(_L("Extrude"))); break;
    case Tool::Dressup: m_hdr_dressup->SetLabel(title(
                            m_dressup_type->GetSelection() == 0 ? _L("Fillet") : _L("Chamfer"))); break;
    case Tool::Hole:    m_hdr_hole->SetLabel(title(_L("Hole")));       break;
    case Tool::Thread:  m_hdr_thread->SetLabel(title(_L("Thread")));   break;
    case Tool::None:    break;
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
