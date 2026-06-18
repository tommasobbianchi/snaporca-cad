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
    m_constrain_entities = false;
    m_pick0 = m_pick1 = m_pick2 = -1;
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
    m_constrain_entities = false;
    m_pick0 = m_pick1 = m_pick2 = -1;
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
    m_constrain_entities = false;
    m_pick0 = m_pick1 = m_pick2 = -1;
    m_active = true;
}

void DesignSketchTool::begin_constrain_entities(const std::vector<SketchEntity>& ents,
                                                const SketchPlane& plane)
{
    m_plane = plane;
    m_mode = Mode::Constrain;
    m_constrain_entities = true;
    m_points.clear();
    m_entities = ents;
    m_has_cursor = false;
    m_sel_a = m_sel_b = -1;
    m_pick0 = m_pick1 = m_pick2 = -1;
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

void DesignSketchTool::append_entities(const std::vector<SketchEntity>& ents)
{
    for (const SketchEntity& e : ents)
        m_entities.push_back(e);
}

static double wrap_2pi(double a)
{
    while (a < 0.0)        a += 2.0 * M_PI;
    while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
    return a;
}

// Circumcircle of 3 points. Returns false if (nearly) collinear.
static bool circumcircle(const Vec2d& a, const Vec2d& b, const Vec2d& c,
                         Vec2d& center, double& radius)
{
    const double d = 2.0 * (a.x() * (b.y() - c.y()) +
                            b.x() * (c.y() - a.y()) +
                            c.x() * (a.y() - b.y()));
    if (std::abs(d) < 1e-9)
        return false;
    const double a2 = a.squaredNorm(), b2 = b.squaredNorm(), c2 = c.squaredNorm();
    const double ux = (a2 * (b.y() - c.y()) + b2 * (c.y() - a.y()) + c2 * (a.y() - b.y())) / d;
    const double uy = (a2 * (c.x() - b.x()) + b2 * (a.x() - c.x()) + c2 * (b.x() - a.x())) / d;
    center = Vec2d(ux, uy);
    radius = (a - center).norm();
    return true;
}

// Build an Arc entity that sweeps start -> end passing through `through`. The
// kernel reconstructs the mid from (start_angle+end_angle)/2, so the angle pair
// must bracket `through` on the correct side of the circle.
static SketchEntity make_arc_through(const Vec2d& center, double radius,
                                     const Vec2d& start, const Vec2d& end,
                                     const Vec2d& through, bool construction)
{
    const double a_start = std::atan2(start.y()  - center.y(), start.x()  - center.x());
    const double a_end   = std::atan2(end.y()    - center.y(), end.x()    - center.x());
    const double a_thru  = std::atan2(through.y() - center.y(), through.x() - center.x());
    const double de = wrap_2pi(a_end  - a_start);   // CCW sweep to end (0,2π)
    const double d3 = wrap_2pi(a_thru - a_start);   // CCW position of through
    SketchEntity e;
    e.type   = SketchEntity::Type::Arc;
    e.center = center;
    e.radius = radius;
    e.p0     = start;
    e.p1     = end;
    e.start_angle = a_start;
    e.end_angle   = (d3 <= de) ? (a_start + de) : (a_start + de - 2.0 * M_PI);
    e.construction = construction;
    return e;
}

std::vector<SketchEntity> DesignSketchTool::make_three_point_circle(const Vec2d& a, const Vec2d& b, const Vec2d& c) const
{
    Vec2d center; double radius;
    if (!circumcircle(a, b, c, center, radius))
        return {};
    SketchEntity e;
    e.type = SketchEntity::Type::Circle;
    e.center = center;
    e.p0 = center;
    e.radius = radius;
    e.construction = m_construction;
    return { e };
}

std::vector<SketchEntity> DesignSketchTool::make_three_point_arc(const Vec2d& start, const Vec2d& end, const Vec2d& on_arc) const
{
    Vec2d center; double radius;
    if (!circumcircle(start, end, on_arc, center, radius))
        return {};
    return { make_arc_through(center, radius, start, end, on_arc, m_construction) };
}

std::vector<SketchEntity> DesignSketchTool::make_tangent_arc(const Vec2d& start, const Vec2d& end) const
{
    // Tangent direction at `start` = exit direction of the previous entity.
    Vec2d t(1, 0);
    bool have_t = false;
    if (!m_entities.empty()) {
        const SketchEntity& prev = m_entities.back();
        if (prev.type == SketchEntity::Type::Line) {
            t = prev.p1 - prev.p0; have_t = (t.squaredNorm() > 1e-12);
        } else if (prev.type == SketchEntity::Type::Arc) {
            // Tangent at the arc end p1 is perpendicular to its radius, in the
            // sweep direction.
            const Vec2d r = prev.p1 - prev.center;
            const double sweep = prev.end_angle - prev.start_angle;
            t = (sweep >= 0.0) ? Vec2d(-r.y(), r.x()) : Vec2d(r.y(), -r.x());
            have_t = (r.squaredNorm() > 1e-12);
        }
    }
    const Vec2d se = end - start;
    if (!have_t || se.squaredNorm() < 1e-12) {
        // No tangent reference or zero length: fall back to a straight line.
        SketchEntity e; e.type = SketchEntity::Type::Line; e.p0 = start; e.p1 = end;
        e.construction = m_construction;
        return { e };
    }
    t.normalize();
    const Vec2d n(-t.y(), t.x());            // unit normal to the tangent
    const double denom = 2.0 * n.dot(se);
    if (std::abs(denom) < 1e-9) {            // end lies along the tangent: line
        SketchEntity e; e.type = SketchEntity::Type::Line; e.p0 = start; e.p1 = end;
        e.construction = m_construction;
        return { e };
    }
    const double R = se.squaredNorm() / denom;   // signed radius along n
    const Vec2d center = start + n * R;
    const double radius = std::abs(R);
    // Mid of the tangent arc: project the chord midpoint outward onto the circle.
    const Vec2d chord_mid = (start + end) * 0.5;
    Vec2d to_mid = chord_mid - center;
    if (to_mid.squaredNorm() < 1e-12) to_mid = n;
    to_mid.normalize();
    const Vec2d through = center + to_mid * radius;
    return { make_arc_through(center, radius, start, end, through, m_construction) };
}

std::vector<SketchEntity> DesignSketchTool::make_slot(const Vec2d& c0, const Vec2d& c1, double half_width) const
{
    std::vector<SketchEntity> out;
    Vec2d u = c1 - c0;
    if (u.squaredNorm() < 1e-12 || half_width < 1e-6)
        return out;
    u.normalize();
    const Vec2d nrm(-u.y(), u.x());
    const double w = half_width;
    // Names avoid termios macros (B0 is a baud-rate #define pulled in transitively).
    const Vec2d top0 = c0 + nrm * w, top1 = c1 + nrm * w;   // upper side (c0 -> c1)
    const Vec2d bot1 = c1 - nrm * w, bot0 = c0 - nrm * w;   // lower side (c1 -> c0)

    auto line = [&](const Vec2d& p0, const Vec2d& p1) {
        SketchEntity e; e.type = SketchEntity::Type::Line; e.p0 = p0; e.p1 = p1;
        e.construction = m_construction; return e;
    };
    out.push_back(line(top0, top1));                                   // top
    out.push_back(make_arc_through(c1, w, top1, bot1, c1 + u * w, m_construction)); // cap @c1 (+u)
    out.push_back(line(bot1, bot0));                                   // bottom
    out.push_back(make_arc_through(c0, w, bot0, top0, c0 - u * w, m_construction)); // cap @c0 (-u)
    return out;
}

std::vector<SketchEntity> DesignSketchTool::make_polygon(const Vec2d& center, const Vec2d& vertex, int sides) const
{
    std::vector<SketchEntity> out;
    if (sides < 3) sides = 3;
    const Vec2d rv = vertex - center;
    const double R = rv.norm();
    if (R < 1e-6) return out;
    const double a0 = std::atan2(rv.y(), rv.x());
    std::vector<Vec2d> verts; verts.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const double a = a0 + 2.0 * M_PI * double(i) / double(sides);
        verts.push_back(Vec2d(center.x() + R * std::cos(a), center.y() + R * std::sin(a)));
    }
    for (int i = 0; i < sides; ++i) {
        SketchEntity e; e.type = SketchEntity::Type::Line;
        e.p0 = verts[i]; e.p1 = verts[(i + 1) % sides];
        e.construction = m_construction;
        out.push_back(e);
    }
    return out;
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

void DesignSketchTool::draw_entities_preview(const std::vector<SketchEntity>& ents, const ColorRGBA& color)
{
    for (const SketchEntity& e : ents) {
        if (e.type == SketchEntity::Type::Point) continue;
        bool closed = false;
        std::vector<Vec2d> poly = entity_polyline(e, closed);
        draw_quad_strip(m_highlight_model, poly, closed, color);
    }
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
        if (m_constrain_entities) {
            // Draw all entities cyan; picked Line entities highlighted red.
            std::vector<Vec2d> markers;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                const SketchEntity& e = m_entities[i];
                const bool sel = (int(i) == m_pick0 || int(i) == m_pick1 || int(i) == m_pick2);
                const ColorRGBA col = sel ? red : cyan;
                if (e.type == SketchEntity::Type::Point) { markers.push_back(e.p0); continue; }
                bool closed = false;
                std::vector<Vec2d> poly = entity_polyline(e, closed);
                draw_quad_strip(sel ? m_highlight_model : m_line_model, poly, closed, col);
            }
            if (!markers.empty())
                draw_vertices(m_vertex_model, markers, cyan);
            shader->stop_using();
            glsafe(::glEnable(GL_CULL_FACE));
            glsafe(::glEnable(GL_DEPTH_TEST));
            return;
        }
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

    case Mode::ThreePointCircle: {
        draw_vertices(m_vertex_model, m_points, yellow);
        if (m_points.size() == 2 && m_has_cursor)
            draw_entities_preview(make_three_point_circle(m_points[0], m_points[1], m_cursor), preview);
        break;
    }

    case Mode::ThreePointArc: {
        draw_vertices(m_vertex_model, m_points, yellow);
        if (m_points.size() == 2 && m_has_cursor)
            draw_entities_preview(make_three_point_arc(m_points[0], m_points[1], m_cursor), preview);
        break;
    }

    case Mode::TangentArc: {
        draw_vertices(m_vertex_model, m_points, yellow);
        if (m_points.size() == 1 && m_has_cursor)
            draw_entities_preview(make_tangent_arc(m_points[0], m_cursor), preview);
        break;
    }

    case Mode::Slot: {
        draw_vertices(m_vertex_model, m_points, yellow);
        if (m_points.size() == 1 && m_has_cursor) {
            draw_quad_strip(m_highlight_model, { m_points[0], m_cursor }, false, grey);
        } else if (m_points.size() == 2 && m_has_cursor) {
            Vec2d u = m_points[1] - m_points[0];
            if (u.squaredNorm() > 1e-12) {
                u.normalize();
                const Vec2d n(-u.y(), u.x());
                const double w = std::abs(n.dot(m_cursor - m_points[0]));
                draw_entities_preview(make_slot(m_points[0], m_points[1], w), preview);
            }
        }
        break;
    }

    case Mode::Polygon: {
        draw_vertices(m_vertex_model, m_points, yellow);
        if (m_points.size() == 1 && m_has_cursor)
            draw_entities_preview(make_polygon(m_points[0], m_cursor, m_polygon_sides), preview);
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

// Screen-plane distance from p to a sketch entity, for click picking in Constrain
// mode. Circles/arcs measure distance to the ring; points to their position.
static double entity_pick_dist(const Vec2d& p, const SketchEntity& e)
{
    switch (e.type) {
    case SketchEntity::Type::Line:   return point_segment_dist(p, e.p0, e.p1);
    case SketchEntity::Type::Point:  return (p - e.p0).norm();
    case SketchEntity::Type::Circle:
    case SketchEntity::Type::Arc:    return std::abs((p - e.center).norm() - e.radius);
    }
    return 1e30;
}

bool DesignSketchTool::on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas)
{
    // Constrain mode: pick a segment on click; let move/drag fall through so the
    // camera can still orbit while inspecting the sketch.
    if (m_mode == Mode::Constrain) {
        if (m_constrain_entities) {
            // Pick any entity (line/circle/arc/point): rolling two-slot selection.
            if (evt.LeftDown()) {
                Vec2d p;
                screen_to_plane(canvas, evt, p);
                double best = 1e30;
                int bi = -1;
                for (size_t i = 0; i < m_entities.size(); ++i) {
                    const double d = entity_pick_dist(p, m_entities[i]);
                    if (d < best) { best = d; bi = int(i); }
                }
                if (bi >= 0) {
                    // Rolling three-slot selection: slots 0/1 feed all 2-entity
                    // constraints; slot 2 is the Symmetric axis (only filled once
                    // 0 and 1 are set). A click past slot 2 restarts the cycle.
                    if (m_pick0 < 0)                                         { m_pick0 = bi; m_pick0_pt = p; }
                    else if (m_pick1 < 0 && bi != m_pick0)                    m_pick1 = bi;
                    else if (m_pick1 >= 0 && m_pick2 < 0 && bi != m_pick0 && bi != m_pick1) m_pick2 = bi;
                    else                                                     { m_pick0 = bi; m_pick1 = m_pick2 = -1; m_pick0_pt = p; }
                }
                return true;
            }
            if (evt.RightDown()) { cancel(); return true; }
            return false;
        }
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

    case Mode::ThreePointCircle: {
        if (evt.LeftDown()) {
            Vec2d p; screen_to_plane(canvas, evt, p);
            m_points.push_back(p);
            if (m_points.size() == 3) {
                append_entities(make_three_point_circle(m_points[0], m_points[1], m_points[2]));
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::ThreePointArc: {
        if (evt.LeftDown()) {
            Vec2d p; screen_to_plane(canvas, evt, p);
            m_points.push_back(p);
            if (m_points.size() == 3) {
                // clicks: start, end, point-on-arc
                append_entities(make_three_point_arc(m_points[0], m_points[1], m_points[2]));
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::TangentArc: {
        if (evt.LeftDown()) {
            Vec2d p; screen_to_plane(canvas, evt, p);
            m_points.push_back(p);
            if (m_points.size() == 2) {
                append_entities(make_tangent_arc(m_points[0], m_points[1]));
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::Slot: {
        if (evt.LeftDown()) {
            Vec2d p; screen_to_plane(canvas, evt, p);
            if (m_points.size() < 2) {
                m_points.push_back(p);
            } else {
                // third click sets the half-width (distance to the centerline)
                Vec2d u = m_points[1] - m_points[0];
                if (u.squaredNorm() > 1e-12) {
                    u.normalize();
                    const Vec2d n(-u.y(), u.x());
                    const double w = std::abs(n.dot(p - m_points[0]));
                    append_entities(make_slot(m_points[0], m_points[1], w));
                }
                m_points.clear();
            }
            return true;
        }
        if (evt.RightDown()) { m_points.clear(); return true; }
        break;
    }

    case Mode::Polygon: {
        if (evt.LeftDown()) {
            Vec2d p; screen_to_plane(canvas, evt, p);
            if (m_points.empty()) {
                m_points.push_back(p);
            } else {
                append_entities(make_polygon(m_points[0], p, m_polygon_sides));
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
