#include "DesignSketchTool.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "3DScene.hpp"
#include "GLShader.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

void DesignSketchTool::begin(const SketchPlane& plane)
{
    m_plane = plane;
    m_points.clear();
    m_has_cursor = false;
    m_active = true;
}

void DesignSketchTool::cancel()
{
    m_active = false;
    m_points.clear();
    m_has_cursor = false;
}

bool DesignSketchTool::screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const
{
    Point pos(evt.GetX(), evt.GetY());
    Linef3 r = canvas.mouse_ray(pos);
    out = m_plane.project(r.a, r.vector());
    return true;
}

bool DesignSketchTool::near_first(const Vec2d& p) const
{
    return m_points.size() >= 3 && (p - m_points[0]).squaredNorm() < 4.0; // close_tol = 2.0 mm
}

bool DesignSketchTool::on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    if (evt.Moving()) {
        screen_to_plane(canvas, evt, m_cursor);
        m_has_cursor = true;
        return true;
    }

    if (evt.LeftDown()) {
        Vec2d p;
        screen_to_plane(canvas, evt, p);
        if (near_first(p)) {
            commit();
            return true;
        }
        m_points.push_back(p);
        return true;
    }

    if (evt.LeftDClick()) {
        if (m_points.size() >= 3)
            commit();
        return true;
    }

    if (evt.RightDown()) {
        if (m_points.size() >= 3)
            commit();
        else
            cancel();
        return true;
    }

    if (evt.Dragging())
        return false; // let camera orbit/pan

    return false;
}

void DesignSketchTool::render(GLCanvas3D& canvas)
{
    (void)canvas;
    if (m_points.empty())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    // Plane-space polyline: committed points + the rubber-band cursor.
    std::vector<Vec2d> pts = m_points;
    if (m_has_cursor)
        pts.push_back(m_cursor);

    glsafe(::glDisable(GL_DEPTH_TEST));
    // Overlay quads are two-sided: their winding flips with segment direction, so
    // disable face culling or half the segments would be culled.
    glsafe(::glDisable(GL_CULL_FACE));
    shader->start_using();
    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    // Segments as thin quads (2 triangles each). LineStrip/Points GL primitives do
    // not render reliably with llvmpipe here, but triangles do — and thick quads
    // read better (Onshape-like). hw = half line width, in plane mm.
    {
        const double hw = 0.6;
        GLModel::Geometry g;
        g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        unsigned int base = 0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec2d a = pts[i], b = pts[i + 1];
            const Vec2d d = b - a;
            const double len = d.norm();
            if (len < 1e-6)
                continue;
            const Vec2d n(-d.y() / len, d.x() / len);
            const Vec2d o = n * hw;
            g.add_vertex((Vec3f)m_plane.to_world(a + o).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(b + o).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(b - o).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(a - o).cast<float>());
            g.add_triangle(base, base + 1, base + 2);
            g.add_triangle(base, base + 2, base + 3);
            base += 4;
        }
        if (base > 0) {
            m_line_model.reset();
            m_line_model.init_from(std::move(g));
            m_line_model.set_color(ColorRGBA(1.0f, 0.55f, 0.1f, 1.0f));
            m_line_model.render();
        }
    }

    // Vertex markers (small squares) at the committed points.
    {
        const double hs = 1.3;
        GLModel::Geometry g;
        g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        unsigned int base = 0;
        for (const Vec2d& p : m_points) {
            g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d(-hs, -hs)).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d( hs, -hs)).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d( hs,  hs)).cast<float>());
            g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d(-hs,  hs)).cast<float>());
            g.add_triangle(base, base + 1, base + 2);
            g.add_triangle(base, base + 2, base + 3);
            base += 4;
        }
        if (base > 0) {
            m_vertex_model.reset();
            m_vertex_model.init_from(std::move(g));
            m_vertex_model.set_color(ColorRGBA(1.0f, 0.85f, 0.2f, 1.0f));
            m_vertex_model.render();
        }
    }

    shader->stop_using();
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
}

void DesignSketchTool::commit()
{
    SketchProfile prof;
    prof.points = m_points;
    prof.closed = true;
    auto cb = on_commit;
    SketchPlane pl = m_plane;
    m_active = false;
    m_points.clear();
    m_has_cursor = false;
    if (cb)
        cb(prof, pl);
}

}} // namespace Slic3r::GUI
