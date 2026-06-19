#include "DesignCanvas.hpp"

#include "GLCanvas3D.hpp"
#include "OpenGLManager.hpp"
#include "3DBed.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "3DScene.hpp"
#include "libslic3r/Config.hpp"

#include <wx/glcanvas.h>
#include <wx/sizer.h>

namespace Slic3r {
namespace GUI {

DesignCanvas::DesignCanvas(wxWindow* parent)
    : wxPanel()
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0))
        return;

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return;

    m_canvas = new GLCanvas3D(m_canvas_widget, m_bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(wxGetApp().plater()->config());
    m_canvas->set_model(&m_model);
    // Reuse the editor's shared slicing process: GLCanvas3D::render() (via
    // _max_bounding_box) dereferences the process when canvas type == View3D.
    // Passing nullptr segfaults; this mirrors View3D/Preview/AssembleView.
    m_canvas->set_process(wxGetApp().plater()->get_background_process());
    m_canvas->set_type(GLCanvas3D::ECanvasType::CanvasView3D);

    m_canvas->enable_picking(false);   // viewport face/edge picking is custom (TODO)
    m_canvas->enable_moving(false);
    m_canvas->enable_gizmos(false);
    m_canvas->enable_selection(false); // stock volume selection unused; solid highlight is tree-driven
    m_canvas->enable_main_toolbar(false);
    m_canvas->enable_select_plate_toolbar(false);
    m_canvas->enable_assemble_view_toolbar(false);
    m_canvas->enable_separator_toolbar(false);
    m_canvas->enable_collapse_toolbar(false);
    m_canvas->enable_plate_chrome(false);
    m_canvas->enable_labels(false);

    m_canvas->set_design_sketch_tool(&m_sketch_tool);
    m_sketch_tool.on_commit = [this](const SketchProfile& prof, const SketchPlane& pl) {
        if (m_on_sketch_commit) m_on_sketch_commit(prof, pl);
        if (m_canvas) m_canvas->set_as_dirty();
        if (m_canvas_widget) m_canvas_widget->Refresh();
    };
    m_sketch_tool.on_commit_entities = [this](const std::vector<SketchEntity>& ents,
                                              const std::vector<SketchEntityConstraintDef>& cons,
                                              const SketchPlane& pl) {
        if (m_on_sketch_entities_commit) m_on_sketch_entities_commit(ents, cons, pl);
        if (m_canvas) m_canvas->set_as_dirty();
        if (m_canvas_widget) m_canvas_widget->Refresh();
    };

    const DynamicPrintConfig* config = wxGetApp().plater()->config();
    if (config) {
        const auto* bed_shape_opt = config->opt<ConfigOptionPoints>("printable_area");
        if (bed_shape_opt) {
            double printable_height = 100.0;
            const auto* ph_opt = config->opt<ConfigOptionFloat>("printable_height");
            if (ph_opt) printable_height = ph_opt->value;
            m_bed.set_shape(bed_shape_opt->values, printable_height, "", false);
        }
    }

    m_canvas->bind_event_handlers();

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_canvas_widget, 1, wxEXPAND);
    SetSizer(sizer);
    SetMinSize(wxSize(300, 300));
}

DesignCanvas::~DesignCanvas()
{
    delete m_canvas;
    delete m_canvas_widget;
}

void DesignCanvas::reload(bool keep_view)
{
    m_canvas->reset_volumes();

    for (int i = 0; i < (int)m_model.objects.size(); ++i)
        m_canvas->load_object(m_model, i);

    const ColorRGBA gold(0.86f, 0.66f, 0.20f, 1.0f);
    const ColorRGBA sel_gold(0.40f, 0.82f, 1.0f, 1.0f);   // cyan tint = solid selected
    const ColorRGBA ghost(0.26f, 0.66f, 1.0f, 0.45f);

    const auto& volumes = m_canvas->get_volumes().volumes;
    for (auto* v : volumes) {
        int obj_idx = v->object_idx();
        if (obj_idx == 0)
            v->set_color(m_body_selected ? sel_gold : gold);
        else if (obj_idx == 1)
            v->set_color(ghost);
    }

    if (!keep_view) {
        if (m_first_frame && !m_model.objects.empty()) {
            m_canvas->select_view("iso");
            m_canvas->zoom_to_volumes();
            m_first_frame = false;
        }
    }

    m_canvas->set_as_dirty();
    if (m_canvas_widget)
        m_canvas_widget->Refresh();
}

void DesignCanvas::set_mesh(const TriangleMesh& mesh)
{
    if (m_model.objects.empty()) {
        auto* obj = m_model.add_object();
        obj->add_volume(mesh);
        obj->add_instance();
    } else {
        ModelObject* obj = m_model.objects.front();
        obj->clear_volumes();
        obj->add_volume(mesh);
        if (obj->instances.empty())
            obj->add_instance();
    }

    reload(!m_first_frame);
}

void DesignCanvas::clear_mesh()
{
    if (!m_model.objects.empty()) {
        m_model.delete_object((size_t)0);
        reload(true);
    }
}

void DesignCanvas::set_preview_mesh(const TriangleMesh& mesh)
{
    // Remove existing ghost (object 1) if present
    if (m_model.objects.size() > 1)
        m_model.delete_object((size_t)1);

    auto* obj = m_model.add_object();
    obj->add_volume(mesh);
    obj->add_instance();

    reload(true);
}

void DesignCanvas::clear_preview()
{
    if (m_model.objects.size() > 1) {
        m_model.delete_object((size_t)1);
        reload(true);
    }
}

void DesignCanvas::fit_view()
{
    if (m_canvas && !m_model.objects.empty()) {
        m_canvas->zoom_to_volumes();
        m_canvas->set_as_dirty();
        if (m_canvas_widget)
            m_canvas_widget->Refresh();
    }
}

void DesignCanvas::set_view(const std::string& view_name)
{
    if (m_canvas) {
        m_canvas->select_view(view_name);
        m_canvas->zoom_to_volumes();
        m_canvas->set_as_dirty();
        if (m_canvas_widget)
            m_canvas_widget->Refresh();
    }
}

void DesignCanvas::begin_sketch(const SketchPlane& plane, DesignSketchTool::Mode mode)
{
    m_sketch_tool.begin(plane, mode);
    if (m_canvas) m_canvas->set_as_dirty();
    if (m_canvas_widget) m_canvas_widget->Refresh();
}

void DesignCanvas::set_sketch_tool(DesignSketchTool::Mode mode)
{
    m_sketch_tool.set_tool(mode);
    if (m_canvas) m_canvas->set_as_dirty();
    if (m_canvas_widget) m_canvas_widget->Refresh();
}

void DesignCanvas::set_sketch_construction(bool c)
{
    m_sketch_tool.set_construction(c);
}

void DesignCanvas::set_sketch_polygon_sides(int n)
{
    m_sketch_tool.set_polygon_sides(n);
}

void DesignCanvas::set_sketch_polygon_circumscribed(bool c)
{
    m_sketch_tool.set_polygon_circumscribed(c);
}

void DesignCanvas::finish_sketch()
{
    m_sketch_tool.finish();
    if (m_canvas) m_canvas->set_as_dirty();
    if (m_canvas_widget) m_canvas_widget->Refresh();
}

bool DesignCanvas::is_sketching() const { return m_sketch_tool.is_active(); }

void DesignCanvas::cancel_sketch()
{
    m_sketch_tool.cancel();
    if (m_canvas) m_canvas->set_as_dirty();
    if (m_canvas_widget) m_canvas_widget->Refresh();
}

void DesignCanvas::set_on_sketch_commit(std::function<void(const SketchProfile&, const SketchPlane&)> cb)
{
    m_on_sketch_commit = std::move(cb);
}

void DesignCanvas::set_on_sketch_entities_commit(
    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> cb)
{
    m_on_sketch_entities_commit = std::move(cb);
}

void DesignCanvas::set_on_segment_drawn(std::function<void(double, double)> cb)
{
    m_sketch_tool.on_segment_drawn = std::move(cb);
}

void DesignCanvas::set_on_cursor_metrics(std::function<void(double, double, bool)> cb)
{
    m_sketch_tool.on_cursor_metrics = std::move(cb);
}

void DesignCanvas::set_on_solve_state(std::function<void(int, bool, bool)> cb)
{
    m_sketch_tool.on_solve_state = std::move(cb);
}

void DesignCanvas::apply_segment_length(double len)
{
    m_sketch_tool.apply_segment_length(len);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::keep_segment_as_drawn()
{
    m_sketch_tool.keep_segment_as_drawn();
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::set_on_sketch_selection_changed(std::function<void(int)> cb)
{
    m_sketch_tool.on_selection_changed = std::move(cb);
}

void DesignCanvas::set_on_sketch_face_selected(std::function<void()> cb)
{
    m_sketch_tool.on_face_selected = std::move(cb);
}

void DesignCanvas::set_display_sketches(std::vector<DesignSketchTool::DisplaySketch> ds)
{
    m_sketch_tool.set_display_sketches(std::move(ds));
    if (m_canvas) m_canvas->set_as_dirty();
    if (m_canvas_widget) m_canvas_widget->Refresh();
}

void DesignCanvas::set_body_highlight(bool on)
{
    if (m_body_selected == on) return;
    m_body_selected = on;
    reload(true);   // recolours the body volume (selected = cyan tint)
}

void DesignCanvas::delete_selected_sketch_entities()
{
    m_sketch_tool.delete_selected();
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::clear_sketch_selection()
{
    m_sketch_tool.clear_selection();
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

DesignSketchTool::DimType DesignCanvas::sketch_dimension_kind() const
{
    return m_sketch_tool.dimension_kind();
}

double DesignCanvas::sketch_dimension_current() const
{
    return m_sketch_tool.dimension_current();
}

void DesignCanvas::apply_sketch_dimension(double v)
{
    m_sketch_tool.apply_dimension(v);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::set_on_dimension_pick_complete(std::function<void(double)> cb)
{
    m_sketch_tool.on_dimension_pick_complete = std::move(cb);
}

DesignSketchTool::DimType DesignCanvas::pending_dimension_type() const
{
    return m_sketch_tool.pending_dimension_type();
}

void DesignCanvas::set_sketch_dimension_value(double v)
{
    m_sketch_tool.set_dimension_value(v);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::cancel_sketch_dimension()
{
    m_sketch_tool.cancel_dimension_value();
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::begin_constrain(const SketchProfile& prof, const SketchPlane& plane)
{
    m_sketch_tool.begin_constrain(prof, plane);
    // The overlay must appear immediately (no mouse move to trigger a repaint);
    // a direct render() is the proven path under llvmpipe.
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::end_constrain()
{
    // cancel() clears m_active + the picked-segment/entity indices, so the
    // constrain overlay (highlighted picks) disappears on the next render.
    m_sketch_tool.cancel();
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

bool DesignCanvas::is_constraining() const { return m_sketch_tool.is_constraining(); }

bool DesignCanvas::selected_segment(int& a, int& b) const
{
    return m_sketch_tool.selected_segment(a, b);
}

void DesignCanvas::update_constrain_profile(const std::vector<Vec2d>& pts)
{
    m_sketch_tool.set_profile_points(pts);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::begin_constrain_entities(const std::vector<SketchEntity>& ents,
                                            const SketchPlane& plane)
{
    m_sketch_tool.begin_constrain_entities(ents, plane);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

bool DesignCanvas::is_constraining_entities() const
{
    return m_sketch_tool.is_constraining_entities();
}

bool DesignCanvas::selected_constrain_entities(int& e0, int& e1) const
{
    return m_sketch_tool.selected_constrain_entities(e0, e1);
}

int DesignCanvas::selected_constrain_axis() const
{
    return m_sketch_tool.pick2();
}

bool DesignCanvas::pick0_point(Vec2d& out) const
{
    return m_sketch_tool.pick0_point(out);
}

void DesignCanvas::update_constrain_entities(const std::vector<SketchEntity>& ents)
{
    m_sketch_tool.set_constrain_entities(ents);
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::set_constraint_highlight(std::vector<int> entities)
{
    m_sketch_tool.set_constraint_highlight(std::move(entities));
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

void DesignCanvas::set_constraint_glyphs(std::vector<SketchEntityConstraintDef> cons)
{
    m_sketch_tool.set_constraint_glyphs(std::move(cons));
    if (m_canvas) { m_canvas->set_as_dirty(); m_canvas->render(); }
}

}} // namespace Slic3r::GUI
