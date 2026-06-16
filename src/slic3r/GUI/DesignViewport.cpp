// GLEW must be included before any header that pulls in <GL/gl.h> (wx/glcanvas.h).
#include <GL/glew.h>

#include "DesignViewport.hpp"

#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/dcclient.h>

#include <algorithm>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r { namespace GUI {

static const double DESIGN_PI = 3.14159265358979323846;

DesignViewport::DesignViewport(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    // Build the GL widget through the manager so its pixel format matches the
    // one the shared wxGLContext was created against (required for SetCurrent).
    m_canvas = OpenGLManager::create_wxglcanvas(*this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_canvas, 1, wxEXPAND);
    SetSizer(sizer);

    m_canvas->Bind(wxEVT_PAINT,            &DesignViewport::on_paint, this);
    m_canvas->Bind(wxEVT_SIZE,             &DesignViewport::on_size,  this);
    m_canvas->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    m_canvas->Bind(wxEVT_LEFT_DOWN,        &DesignViewport::on_mouse, this);
    m_canvas->Bind(wxEVT_LEFT_UP,          &DesignViewport::on_mouse, this);
    m_canvas->Bind(wxEVT_MOTION,           &DesignViewport::on_mouse, this);
    m_canvas->Bind(wxEVT_MOUSEWHEEL,       &DesignViewport::on_mouse, this);

    SetMinSize(wxSize(300, 300));
}

DesignViewport::~DesignViewport()
{
    if (m_context != nullptr && m_canvas != nullptr) {
        m_canvas->SetCurrent(*m_context);
        m_model.reset();
    }
}

void DesignViewport::set_mesh(const TriangleMesh& mesh)
{
    m_pending_mesh = mesh;
    m_mesh_dirty   = true;
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

void DesignViewport::clear_mesh()
{
    m_pending_mesh = TriangleMesh();
    m_mesh_dirty   = true;
    m_has_mesh     = false;
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

void DesignViewport::on_paint(wxPaintEvent& /*evt*/)
{
    // A wxPaintDC is mandatory inside an EVT_PAINT handler to validate the region.
    wxPaintDC dc(m_canvas);
    render();
}

void DesignViewport::on_size(wxSizeEvent& evt)
{
    if (m_canvas != nullptr)
        m_canvas->Refresh();
    evt.Skip();
}

void DesignViewport::on_mouse(wxMouseEvent& evt)
{
    const Vec2d pos(evt.GetX(), evt.GetY());

    if (evt.LeftDown()) {
        m_dragging   = true;
        m_last_mouse = pos;
        if (!m_canvas->HasCapture())
            m_canvas->CaptureMouse();
    }
    else if (evt.LeftUp()) {
        m_dragging = false;
        if (m_canvas->HasCapture())
            m_canvas->ReleaseMouse();
    }
    else if (evt.Dragging() && m_dragging && evt.LeftIsDown()) {
        const Vec2d d = pos - m_last_mouse;
        m_last_mouse  = pos;
        // Mirror GLCanvas3D: pixels -> radians via (PI * TRACKBALLSIZE / 180).
        const double k = DESIGN_PI * 0.8 / 180.0;
        m_camera.rotate_on_sphere(d.x() * k, d.y() * k, true);
        m_canvas->Refresh();
    }
    else if (evt.GetWheelRotation() != 0 && evt.GetWheelDelta() != 0) {
        const double delta = double(evt.GetWheelRotation()) / double(evt.GetWheelDelta());
        m_camera.update_zoom(delta);
        m_canvas->Refresh();
    }
    evt.Skip();
}

void DesignViewport::render()
{
    if (m_canvas == nullptr || !m_canvas->IsShownOnScreen())
        return;

    if (m_context == nullptr)
        m_context = wxGetApp().init_glcontext(*m_canvas);
    if (m_context == nullptr)
        return;

    m_canvas->SetCurrent(*m_context);

    const wxSize sz = m_canvas->GetClientSize();
    const int w = std::max(1, sz.GetWidth());
    const int h = std::max(1, sz.GetHeight());

    // Deferred GPU upload: only touch GL while the context is current.
    if (m_mesh_dirty) {
        m_model.reset();
        if (!m_pending_mesh.its.indices.empty()) {
            m_model.init_from(m_pending_mesh);
            m_bbox          = m_pending_mesh.bounding_box();
            m_has_mesh      = true;
            m_camera_framed = false;
        } else {
            m_has_mesh = false;
        }
        m_mesh_dirty = false;
    }

    m_camera.set_viewport(0, 0, w, h);
    m_camera.apply_viewport();

    if (m_has_mesh && !m_camera_framed) {
        m_camera.set_scene_box(m_bbox);
        m_camera.set_target(m_bbox.center());
        m_camera.select_view("iso");
        m_camera.zoom_to_box(m_bbox);
        m_camera_framed = true;
    }

    ::glClearColor(0.16f, 0.16f, 0.17f, 1.0f);
    ::glEnable(GL_DEPTH_TEST);
    ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_has_mesh) {
        m_camera.apply_projection(m_bbox);

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();

            const Transform3d& view  = m_camera.get_view_matrix();
            const Transform3d  model = Transform3d::Identity();

            shader->set_uniform("projection_matrix", m_camera.get_projection_matrix());
            shader->set_uniform("view_model_matrix", view * model);
            const Matrix3d view_normal_matrix =
                view.matrix().block(0, 0, 3, 3) * model.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            shader->set_uniform("emission_factor", 0.25f);

            m_model.set_color(ColorRGBA{ 0.86f, 0.66f, 0.20f, 1.0f });
            m_model.render();

            shader->stop_using();
        }
    }

    m_canvas->SwapBuffers();
}

}} // namespace Slic3r::GUI
