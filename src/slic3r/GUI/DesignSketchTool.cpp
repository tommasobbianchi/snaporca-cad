#include "DesignSketchTool.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "3DScene.hpp"
#include "GLShader.hpp"

#include <GL/glew.h>
#include <cmath>

namespace Slic3r {
namespace GUI {

void DesignSketchTool::begin(const SketchPlane& plane, Mode mode)
{
    m_plane = plane;
    m_mode = mode;
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
    return m_points.size() >= 3 && (p - m_points[0]).squaredNorm() < 4.0;
}

static std::vector<Vec2d> circle_polygon(const Vec2d& c, double r, int n = 48)
{
    std::vector<Vec2d> v; v.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * double(i) / double(n);
        v.push_back(Vec2d(c.x() + r * std::cos(a), c.y() + r * std::sin(a)));
    }
    return v;
}

void DesignSketchTool::draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color)
{
    if (pts.size() < 2)
        return;

    const double hw = 0.6;
    GLModel::Geometry g;
    g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    unsigned int base = 0;

    const size_t segs = closed ? pts.size() : pts.size() - 1;
    for (size_t i = 0; i < segs; ++i) {
        const Vec2d a = pts[i];
        const Vec2d b = pts[(i + 1) % pts.size()];
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
        model.reset();
        model.init_from(std::move(g));
        model.set_color(color);
        model.render();
    }
}

void DesignSketchTool::draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color)
{
    if (pts.empty())
        return;

    const double hs = 1.3;
    GLModel::Geometry g;
    g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    unsigned int base = 0;
    for (const Vec2d& p : pts) {
        g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d(-hs, -hs)).cast<float>());
        g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d( hs, -hs)).cast<float>());
        g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d( hs,  hs)).cast<float>());
        g.add_vertex((Vec3f)m_plane.to_world(p + Vec2d(-hs,  hs)).cast<float>());
        g.add_triangle(base, base + 1, base + 2);
        g.add_triangle(base, base + 2, base + 3);
        base += 4;
    }

    model.reset();
    model.init_from(std::move(g));
    model.set_color(color);
    model.render();
}

void DesignSketchTool::render(GLCanvas3D& canvas)
{
    (void)canvas;
    if (m_points.empty())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    shader->start_using();
    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    const ColorRGBA orange(1.0f, 0.55f, 0.1f, 1.0f);
    const ColorRGBA yellow(1.0f, 0.85f, 0.2f, 1.0f);

    switch (m_mode) {

    case Mode::Polyline: {
        std::vector<Vec2d> pts = m_points;
        if (m_has_cursor)
            pts.push_back(m_cursor);
        draw_quad_strip(m_line_model, pts, false, orange);
        draw_vertices(m_vertex_model, m_points, yellow);
        break;
    }

    case Mode::Rectangle: {
        if (m_points.size() == 1 && m_has_cursor) {
            const Vec2d A = m_points[0];
            const Vec2d B = m_cursor;
            std::vector<Vec2d> corners = { A, Vec2d(B.x(), A.y()), B, Vec2d(A.x(), B.y()) };
            draw_quad_strip(m_line_model, corners, true, orange);
            draw_vertices(m_vertex_model, corners, yellow);
        } else if (!m_points.empty()) {
            draw_quad_strip(m_line_model, m_points, true, orange);
            draw_vertices(m_vertex_model, m_points, yellow);
        }
        break;
    }

    case Mode::Circle: {
        if (m_points.size() == 1 && m_has_cursor) {
            const Vec2d C = m_points[0];
            const double r = (m_cursor - C).norm();
            auto circ = circle_polygon(C, r);
            draw_quad_strip(m_line_model, circ, true, orange);
            std::vector<Vec2d> center = { C };
            draw_vertices(m_vertex_model, center, yellow);
        } else if (m_points.size() >= 3) {
            draw_quad_strip(m_line_model, m_points, true, orange);
            std::vector<Vec2d> center = { m_points[0] };
            draw_vertices(m_vertex_model, center, yellow);
        }
        break;
    }

    }

    shader->stop_using();
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
}

bool DesignSketchTool::on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    if (evt.Moving()) {
        screen_to_plane(canvas, evt, m_cursor);
        m_has_cursor = true;
        return true;
    }

    switch (m_mode) {

    case Mode::Polyline: {
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
        break;
    }

    case Mode::Rectangle: {
        if (evt.LeftDown()) {
            if (m_points.empty()) {
                Vec2d A;
                screen_to_plane(canvas, evt, A);
                m_points.push_back(A);
                return true;
            } else {
                Vec2d B;
                screen_to_plane(canvas, evt, B);
                const Vec2d A = m_points[0];
                m_points = { A, Vec2d(B.x(), A.y()), B, Vec2d(A.x(), B.y()) };
                commit();
                return true;
            }
        }

        if (evt.RightDown()) {
            cancel();
            return true;
        }
        break;
    }

    case Mode::Circle: {
        if (evt.LeftDown()) {
            if (m_points.empty()) {
                Vec2d C;
                screen_to_plane(canvas, evt, C);
                m_points.push_back(C);
                return true;
            } else {
                Vec2d P;
                screen_to_plane(canvas, evt, P);
                const Vec2d C = m_points[0];
                const double r = (P - C).norm();
                if (r < 1e-3)
                    return true;
                m_points = circle_polygon(C, r);
                commit();
                return true;
            }
        }

        if (evt.RightDown()) {
            cancel();
            return true;
        }
        break;
    }

    }

    if (evt.Dragging())
        return false;

    return false;
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
