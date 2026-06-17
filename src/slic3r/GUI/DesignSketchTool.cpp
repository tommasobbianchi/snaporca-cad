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
    m_entities.clear();
    m_construction = false;
    m_has_cursor = false;
    m_sel_a = m_sel_b = -1;
    m_active = true;
}

void DesignSketchTool::set_tool(Mode mode)
{
    // Switch the active drawing tool without dropping accumulated entities.
    m_mode = mode;
    m_points.clear();
    m_has_cursor = false;
}

void DesignSketchTool::cancel()
{
    m_active = false;
    m_points.clear();
    m_entities.clear();
    m_construction = false;
    m_has_cursor = false;
    m_sel_a = m_sel_b = -1;
}

void DesignSketchTool::finish()
{
    auto cb = on_commit_entities;
    std::vector<SketchEntity> ents = m_entities;
    SketchPlane pl = m_plane;
    m_active = false;
    m_points.clear();
    m_entities.clear();
    m_has_cursor = false;
    if (cb)
        cb(ents, pl);
}

void DesignSketchTool::begin_constrain(const SketchProfile& prof, const SketchPlane& plane)
{
    m_plane = plane;
    m_mode = Mode::Constrain;
    m_points = prof.points;
    m_entities.clear();
    m_has_cursor = false;
    m_sel_a = m_sel_b = -1;
    m_active = true;
}

bool DesignSketchTool::selected_segment(int& a, int& b) const
{
    if (m_sel_a < 0 || m_sel_b < 0)
        return false;
    a = m_sel_a;
    b = m_sel_b;
    return true;
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

// ---- entity builders --------------------------------------------------------

void DesignSketchTool::push_line(const Vec2d& a, const Vec2d& b)
{
    if ((b - a).squaredNorm() < 1e-9)
        return;
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0 = a;
    e.p1 = b;
    e.construction = m_construction;
    m_entities.push_back(e);
}

void DesignSketchTool::push_closed_lines(const std::vector<Vec2d>& corners)
{
    const size_t n = corners.size();
    if (n < 2) return;
    for (size_t i = 0; i < n; ++i)
        push_line(corners[i], corners[(i + 1) % n]);
}

void DesignSketchTool::push_open_chain(const std::vector<Vec2d>& pts)
{
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        push_line(pts[i], pts[i + 1]);
}

void DesignSketchTool::push_circle(const Vec2d& center, double radius)
{
    if (radius < 1e-3) return;
    SketchEntity e;
    e.type = SketchEntity::Type::Circle;
    e.center = center;
    e.p0 = center;
    e.radius = radius;
    e.construction = m_construction;
    m_entities.push_back(e);
}

void DesignSketchTool::push_point(const Vec2d& p)
{
    SketchEntity e;
    e.type = SketchEntity::Type::Point;
    e.p0 = p;
    e.center = p;
    e.construction = m_construction;
    m_entities.push_back(e);
}

std::vector<Vec2d> DesignSketchTool::entity_polyline(const SketchEntity& e, bool& closed) const
{
    closed = false;
    switch (e.type) {
    case SketchEntity::Type::Line:
        return { e.p0, e.p1 };
    case SketchEntity::Type::Circle:
        closed = true;
        return circle_polygon(e.center, e.radius);
    case SketchEntity::Type::Arc: {
        const int n = 24;
        std::vector<Vec2d> pts; pts.reserve(n + 1);
        for (int i = 0; i <= n; ++i) {
            const double a = e.start_angle + (e.end_angle - e.start_angle) * double(i) / double(n);
            pts.push_back(Vec2d(e.center.x() + e.radius * std::cos(a),
                                e.center.y() + e.radius * std::sin(a)));
        }
        return pts;
    }
    case SketchEntity::Type::Point:
        return { e.p0 };
    }
    return {};
}

// ---- rendering --------------------------------------------------------------

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
    if (!m_active)
        return;
    if (m_mode != Mode::Constrain && m_entities.empty() && m_points.empty())
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
    const ColorRGBA grey(0.55f, 0.55f, 0.60f, 1.0f);

    if (m_mode == Mode::Constrain) {
        const ColorRGBA cyan(0.30f, 0.80f, 1.0f, 1.0f);
        const ColorRGBA red(1.0f, 0.25f, 0.25f, 1.0f);
        draw_quad_strip(m_line_model, m_points, true, cyan);
        draw_vertices(m_vertex_model, m_points, cyan);
        if (m_sel_a >= 0 && m_sel_b >= 0 &&
            m_sel_a < int(m_points.size()) && m_sel_b < int(m_points.size())) {
            std::vector<Vec2d> seg = { m_points[m_sel_a], m_points[m_sel_b] };
            draw_quad_strip(m_highlight_model, seg, false, red);
        }
        shader->stop_using();
        glsafe(::glEnable(GL_CULL_FACE));
        glsafe(::glEnable(GL_DEPTH_TEST));
        return;
    }

    // Committed entities of this session.
    std::vector<Vec2d> point_markers;
    for (const SketchEntity& e : m_entities) {
        const ColorRGBA col = e.construction ? grey : orange;
        if (e.type == SketchEntity::Type::Point) {
            point_markers.push_back(e.p0);
            continue;
        }
        bool closed = false;
        std::vector<Vec2d> poly = entity_polyline(e, closed);
        draw_quad_strip(m_line_model, poly, closed, col);
    }
    if (!point_markers.empty())
        draw_vertices(m_vertex_model, point_markers, yellow);

    // In-progress entity preview for the active tool.
    const ColorRGBA preview = m_construction ? grey : orange;
    switch (m_mode) {

    case Mode::Polyline: {
        std::vector<Vec2d> pts = m_points;
        if (m_has_cursor)
            pts.push_back(m_cursor);
        draw_quad_strip(m_highlight_model, pts, false, preview);
        draw_vertices(m_vertex_model, m_points, yellow);
        break;
    }

    case Mode::CornerRect: {
        if (m_points.size() == 1 && m_has_cursor) {
            const Vec2d A = m_points[0];
            const Vec2d B = m_cursor;
            std::vector<Vec2d> corners = { A, Vec2d(B.x(), A.y()), B, Vec2d(A.x(), B.y()) };
            draw_quad_strip(m_highlight_model, corners, true, preview);
        }
        break;
    }

    case Mode::CenterRect: {
        if (m_points.size() == 1 && m_has_cursor) {
            const Vec2d C = m_points[0];
            const Vec2d P = m_cursor;
            const double hx = std::abs(P.x() - C.x());
            const double hy = std::abs(P.y() - C.y());
            std::vector<Vec2d> corners = {
                Vec2d(C.x() - hx, C.y() - hy), Vec2d(C.x() + hx, C.y() - hy),
                Vec2d(C.x() + hx, C.y() + hy), Vec2d(C.x() - hx, C.y() + hy) };
            draw_quad_strip(m_highlight_model, corners, true, preview);
            draw_vertices(m_vertex_model, { C }, yellow);
        }
        break;
    }

    case Mode::CenterCircle: {
        if (m_points.size() == 1 && m_has_cursor) {
            const Vec2d C = m_points[0];
            const double r = (m_cursor - C).norm();
            draw_quad_strip(m_highlight_model, circle_polygon(C, r), true, preview);
            draw_vertices(m_vertex_model, { C }, yellow);
        }
        break;
    }

    case Mode::Point:
    case Mode::Constrain:
        break;
    }

    shader->stop_using();
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
}

// Distance from point p to the segment [a,b] in plane (2D) coordinates.
static double point_segment_dist(const Vec2d& p, const Vec2d& a, const Vec2d& b)
{
    const Vec2d ab = b - a;
    const double len2 = ab.squaredNorm();
    if (len2 < 1e-12)
        return (p - a).norm();
    double t = (p - a).dot(ab) / len2;
    t = std::max(0.0, std::min(1.0, t));
    return (p - (a + t * ab)).norm();
}

bool DesignSketchTool::on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    // Constrain mode: pick a segment on click; let move/drag fall through so the
    // camera can still orbit while inspecting the sketch.
    if (m_mode == Mode::Constrain) {
        if (evt.LeftDown()) {
            if (m_points.size() < 2)
                return false;
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            const size_t n = m_points.size();
            double best = 1e30;
            int bi = -1;
            for (size_t i = 0; i < n; ++i) {
                const double d = point_segment_dist(p, m_points[i], m_points[(i + 1) % n]);
                if (d < best) { best = d; bi = int(i); }
            }
            if (bi >= 0) {
                m_sel_a = bi;
                m_sel_b = int((bi + 1) % n);
            }
            return true;
        }
        if (evt.RightDown()) {
            cancel();
            return true;
        }
        return false;
    }

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
                push_closed_lines(m_points);  // close the loop
                m_points.clear();
                return true;
            }
            m_points.push_back(p);
            return true;
        }
        if (evt.LeftDClick()) {
            if (m_points.size() >= 2) {
                push_open_chain(m_points);     // end as an open chain
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) {
            if (m_points.size() >= 3)
                push_closed_lines(m_points);
            else if (m_points.size() == 2)
                push_open_chain(m_points);
            m_points.clear();
            return true;
        }
        break;
    }

    case Mode::CornerRect: {
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            if (m_points.empty()) {
                m_points.push_back(p);
            } else {
                const Vec2d A = m_points[0];
                const Vec2d B = p;
                push_closed_lines({ A, Vec2d(B.x(), A.y()), B, Vec2d(A.x(), B.y()) });
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::CenterRect: {
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            if (m_points.empty()) {
                m_points.push_back(p);
            } else {
                const Vec2d C = m_points[0];
                const double hx = std::abs(p.x() - C.x());
                const double hy = std::abs(p.y() - C.y());
                push_closed_lines({
                    Vec2d(C.x() - hx, C.y() - hy), Vec2d(C.x() + hx, C.y() - hy),
                    Vec2d(C.x() + hx, C.y() + hy), Vec2d(C.x() - hx, C.y() + hy) });
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::CenterCircle: {
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            if (m_points.empty()) {
                m_points.push_back(p);
            } else {
                const Vec2d C = m_points[0];
                push_circle(C, (p - C).norm());
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::Point: {
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            push_point(p);
            return true;
        }
        if (evt.RightDown()) { return true; }
        break;
    }

    case Mode::Constrain:
        break;
    }

    if (evt.Dragging())
        return false;

    return false;
}

}} // namespace Slic3r::GUI
