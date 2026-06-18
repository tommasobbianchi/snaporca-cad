#include "DesignSketchTool.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "3DScene.hpp"
#include "GLShader.hpp"

#include <GL/glew.h>
#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

// Positioning helpers (defined lower down, used by the dimension methods above them).
static bool entity_ref_point(const SketchEntity& e, Vec2d& out);
static void translate_entity(SketchEntity& e, const Vec2d& d);

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
    m_awaiting_length = false;
    m_selection.clear();
    m_constraints.clear();
    m_active = true;
}

void DesignSketchTool::set_tool(Mode mode)
{
    // Switch the active drawing tool without dropping accumulated entities.
    m_mode = mode;
    m_points.clear();
    m_has_cursor = false;
    m_awaiting_length = false;
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
    m_awaiting_length = false;
    m_selection.clear();
    m_constraints.clear();
}

void DesignSketchTool::clear_selection()
{
    if (m_selection.empty()) return;
    m_selection.clear();
    if (on_selection_changed) on_selection_changed(0);
}

void DesignSketchTool::delete_selected()
{
    if (m_selection.empty()) return;
    const int n = int(m_entities.size());
    std::vector<bool> del(n, false);
    for (int i : m_selection)
        if (i >= 0 && i < n) del[i] = true;
    // old index -> new index (or -1 if deleted), to fix up constraint references.
    std::vector<int> remap(n, -1);
    int next = 0;
    for (int i = 0; i < n; ++i)
        if (!del[i]) remap[i] = next++;
    for (int i = n - 1; i >= 0; --i)
        if (del[i]) m_entities.erase(m_entities.begin() + i);
    // Drop constraints touching a deleted entity; remap the survivors.
    std::vector<SketchEntityConstraintDef> kept;
    auto live = [&](int e) { return e < 0 || (e < n && remap[e] >= 0); };
    auto map  = [&](int e) { return e < 0 ? -1 : remap[e]; };
    for (SketchEntityConstraintDef c : m_constraints) {
        if (!live(c.ea) || !live(c.eb) || !live(c.ec)) continue;
        c.ea = map(c.ea); c.eb = map(c.eb); c.ec = map(c.ec);
        kept.push_back(c);
    }
    m_constraints.swap(kept);
    m_selection.clear();
    if (on_selection_changed) on_selection_changed(0);
}

bool DesignSketchTool::selection_valid() const
{
    for (int i : m_selection)
        if (i < 0 || i >= int(m_entities.size())) return false;
    return true;
}

DesignSketchTool::DimType DesignSketchTool::dimension_kind() const
{
    if (!selection_valid()) return DimType::None;
    if (m_selection.size() == 1) {
        switch (m_entities[m_selection[0]].type) {
        case SketchEntity::Type::Line:   return DimType::Length;
        case SketchEntity::Type::Circle: return DimType::Diameter;
        case SketchEntity::Type::Arc:    return DimType::Radius;
        default: return DimType::None;
        }
    }
    if (m_selection.size() == 2) {
        const SketchEntity& a = m_entities[m_selection[0]];
        const SketchEntity& b = m_entities[m_selection[1]];
        const bool aLine = (a.type == SketchEntity::Type::Line);
        const bool bLine = (b.type == SketchEntity::Type::Line);
        Vec2d tmp(0, 0);
        if (aLine && bLine)                                   return DimType::Angle;
        // one line + one point-like (point / circle-centre / arc-centre)
        if (aLine && entity_ref_point(b, tmp))                return DimType::DistanceToLine;
        if (bLine && entity_ref_point(a, tmp))                return DimType::DistanceToLine;
        // two point-likes -> centre/point distance (0 = coincident/concentric)
        if (entity_ref_point(a, tmp) && entity_ref_point(b, tmp)) return DimType::Distance;
    }
    return DimType::None;
}

double DesignSketchTool::dimension_current() const
{
    switch (dimension_kind()) {
    case DimType::Length:   { const auto& e = m_entities[m_selection[0]]; return (e.p1 - e.p0).norm(); }
    case DimType::Diameter: return 2.0 * m_entities[m_selection[0]].radius;
    case DimType::Radius:   return m_entities[m_selection[0]].radius;
    case DimType::Angle: {
        const auto& a = m_entities[m_selection[0]];
        const auto& b = m_entities[m_selection[1]];
        const Vec2d da = a.p1 - a.p0, db = b.p1 - b.p0;
        const double na = da.norm(), nb = db.norm();
        if (na < 1e-9 || nb < 1e-9) return 0.0;
        const double c = std::max(-1.0, std::min(1.0, da.dot(db) / (na * nb)));
        return std::acos(c) * 180.0 / M_PI;
    }
    case DimType::Distance: {
        Vec2d ra(0, 0), rb(0, 0);
        entity_ref_point(m_entities[m_selection[0]], ra);
        entity_ref_point(m_entities[m_selection[1]], rb);
        return (rb - ra).norm();
    }
    case DimType::DistanceToLine: {
        const SketchEntity& a = m_entities[m_selection[0]];
        const SketchEntity& b = m_entities[m_selection[1]];
        const bool aLine = (a.type == SketchEntity::Type::Line);
        const SketchEntity& L = aLine ? a : b;
        const SketchEntity& P = aLine ? b : a;
        Vec2d rp(0, 0); entity_ref_point(P, rp);
        Vec2d dir = L.p1 - L.p0;
        const double n = dir.norm();
        if (n < 1e-9) return 0.0;
        const Vec2d nrm(-dir.y() / n, dir.x() / n);   // unit normal to the line
        return std::abs((rp - L.p0).dot(nrm));
    }
    default: return 0.0;
    }
}

void DesignSketchTool::apply_angle_between(int ia, int ib, double deg)
{
    SketchEntity& A = m_entities[ia];
    SketchEntity& B = m_entities[ib];
    const Vec2d aE[2] = { A.p0, A.p1 };
    const Vec2d bE[2] = { B.p0, B.p1 };
    int si = -1, sj = -1;
    double best = 1e-6;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const double d = (aE[i] - bE[j]).squaredNorm();
            if (d < best) { best = d; si = i; sj = j; }
        }
    Vec2d pivot, refDir, bMoving;
    bool moveP1;
    if (si >= 0) {                       // shared vertex: pivot there, A's arm is the reference
        pivot   = aE[si];
        refDir  = aE[1 - si] - pivot;
        moveP1  = (sj == 0);             // move the B end that is NOT at the pivot
        bMoving = moveP1 ? B.p1 : B.p0;
    } else {                             // no shared vertex: pivot B.p0, A's direction is reference
        pivot   = B.p0;
        refDir  = A.p1 - A.p0;
        moveP1  = true;
        bMoving = B.p1;
    }
    double nr = refDir.norm();
    if (nr < 1e-9) return;
    refDir /= nr;
    const Vec2d db = bMoving - pivot;
    const double Lb = db.norm();
    if (Lb < 1e-9) return;
    const double cross = refDir.x() * db.y() - refDir.y() * db.x();
    const double sign  = (cross >= 0.0) ? 1.0 : -1.0;   // keep B on its current side
    const double rad   = sign * deg * M_PI / 180.0;
    const Vec2d ndir(refDir.x() * std::cos(rad) - refDir.y() * std::sin(rad),
                     refDir.x() * std::sin(rad) + refDir.y() * std::cos(rad));
    const Vec2d nb = pivot + Lb * ndir;
    if (moveP1) B.p1 = nb; else B.p0 = nb;
}

void DesignSketchTool::apply_dimension(double v)
{
    switch (dimension_kind()) {
    case DimType::Length: {
        SketchEntity& e = m_entities[m_selection[0]];
        const Vec2d d = e.p1 - e.p0;
        const double r = d.norm();
        if (r > 1e-9 && v > 1e-9) e.p1 = e.p0 + (v / r) * d;
        break;
    }
    case DimType::Diameter: if (v > 1e-9) m_entities[m_selection[0]].radius = 0.5 * v; break;
    case DimType::Radius:   if (v > 1e-9) m_entities[m_selection[0]].radius = v;       break;
    case DimType::Angle:    apply_angle_between(m_selection[0], m_selection[1], v);     break;
    case DimType::Distance: {
        // Move the 2nd selection so its reference point sits at distance v from the
        // 1st (v == 0 -> coincident / concentric). Translate the whole entity.
        if (v < 0.0) break;
        Vec2d ra(0, 0), rb(0, 0);
        entity_ref_point(m_entities[m_selection[0]], ra);
        SketchEntity& b = m_entities[m_selection[1]];
        entity_ref_point(b, rb);
        const Vec2d d = rb - ra;
        const double r = d.norm();
        Vec2d target = ra;
        if (r > 1e-9) target = ra + (v / r) * d;
        else          target = ra + Vec2d(v, 0.0);
        translate_entity(b, target - rb);
        break;
    }
    case DimType::DistanceToLine: {
        // Move the point-like selection perpendicular to the line so its reference
        // point is at distance v (v == 0 -> on the line / on the axis).
        if (v < 0.0) break;
        const bool aLine = (m_entities[m_selection[0]].type == SketchEntity::Type::Line);
        const SketchEntity& L = m_entities[m_selection[aLine ? 0 : 1]];
        SketchEntity& P = m_entities[m_selection[aLine ? 1 : 0]];
        Vec2d rp(0, 0); entity_ref_point(P, rp);
        Vec2d dir = L.p1 - L.p0;
        const double n = dir.norm();
        if (n < 1e-9) break;
        const Vec2d nrm(-dir.y() / n, dir.x() / n);
        const double d0 = (rp - L.p0).dot(nrm);          // current signed distance
        const double sign = (d0 >= 0.0) ? 1.0 : -1.0;    // keep the point on its side
        translate_entity(P, (sign * v - d0) * nrm);
        break;
    }
    default: break;
    }
    record_dimension_constraint(v);          // store a driving constraint for this dimension
    resolve_live();                          // live-solve so the viewport shows the solved sketch
    m_selection.clear();
    if (on_selection_changed) on_selection_changed(0);
}

// Append the SketchEntityConstraintDef that makes the just-applied dimension a
// driving constraint (enforced by the kernel live and at commit). DistanceToLine
// records a PointOnLine constraint so "centre onto axis" persists through re-solve.
void DesignSketchTool::record_dimension_constraint(double v)
{
    const DimType k = dimension_kind();
    auto role = [&](int i) {
        return (m_entities[i].type == SketchEntity::Type::Point) ? SketchPointRole::P0
                                                                 : SketchPointRole::Center;
    };
    SketchEntityConstraintDef c;
    switch (k) {
    case DimType::Length:
        c.type = SketchConstraintType::Distance;
        c.ea = m_selection[0]; c.ra = SketchPointRole::P0;
        c.eb = m_selection[0]; c.rb = SketchPointRole::P1;
        c.value = v; m_constraints.push_back(c); break;
    case DimType::Diameter:
        c.type = SketchConstraintType::Diameter; c.ea = m_selection[0]; c.value = v;
        m_constraints.push_back(c); break;
    case DimType::Radius:
        c.type = SketchConstraintType::Radius; c.ea = m_selection[0]; c.value = v;
        m_constraints.push_back(c); break;
    case DimType::Angle:
        c.type = SketchConstraintType::Angle;
        c.ea = m_selection[0]; c.eb = m_selection[1]; c.value = v;
        m_constraints.push_back(c); break;
    case DimType::Distance: {
        const int ia = m_selection[0], ib = m_selection[1];
        if (v < 1e-9) c.type = SketchConstraintType::Coincident;
        else        { c.type = SketchConstraintType::Distance; c.value = v; }
        c.ea = ia; c.ra = role(ia); c.eb = ib; c.rb = role(ib);
        m_constraints.push_back(c); break;
    }
    case DimType::DistanceToLine: {
        // Point-on-line driving constraint: hold the point-like entity at unsigned
        // perpendicular distance v from the line (v == 0 -> on the axis).
        const bool aLine = (m_entities[m_selection[0]].type == SketchEntity::Type::Line);
        const int ip = m_selection[aLine ? 1 : 0];   // point-like (Point/Circle/Arc)
        const int il = m_selection[aLine ? 0 : 1];   // line
        c.type = SketchConstraintType::PointOnLine;
        c.ea = ip; c.ra = role(ip); c.eb = il; c.value = v;
        m_constraints.push_back(c); break;
    }
    default: break;   // None: no driving constraint recorded
    }
}

// Onshape-style live solve: enforce all accumulated driving constraints on the
// in-session entities immediately, so the viewport reflects the solved sketch as
// each dimension/constraint is added (not only at commit). The pre-edit geometry
// is the solver's initial guess, keeping convergence local and side-preserving.
void DesignSketchTool::resolve_live()
{
    if (!m_constraints.empty())
        solve_sketch_entities(m_entities, m_constraints);
}

void DesignSketchTool::apply_segment_length(double len)
{
    if (m_points.size() == 2 && len > 1e-9) {
        const Vec2d d = m_points[1] - m_points[0];
        const double r = d.norm();
        if (r > 1e-9)
            m_points[1] = m_points[0] + (len / r) * d;
    }
    keep_segment_as_drawn();
    // Driving length constraint on the just-committed Line entity.
    if (len > 1e-9 && !m_entities.empty()) {
        const int i = int(m_entities.size()) - 1;
        if (m_entities[i].type == SketchEntity::Type::Line) {
            SketchEntityConstraintDef c;
            c.type = SketchConstraintType::Distance;
            c.ea = i; c.ra = SketchPointRole::P0;
            c.eb = i; c.rb = SketchPointRole::P1;
            c.value = len;
            m_constraints.push_back(c);
        }
    }
    resolve_live();                          // live-solve the in-session sketch
}

void DesignSketchTool::keep_segment_as_drawn()
{
    if (m_points.size() == 2)
        push_line(m_points[0], m_points[1]);  // accrues into the session's entities
    m_points.clear();
    m_has_cursor = false;
    m_awaiting_length = false;
}

void DesignSketchTool::finish()
{
    auto cb = on_commit_entities;
    std::vector<SketchEntity> ents = m_entities;
    std::vector<SketchEntityConstraintDef> cons = m_constraints;
    SketchPlane pl = m_plane;
    m_active = false;
    m_points.clear();
    m_entities.clear();
    m_constraints.clear();
    m_has_cursor = false;
    if (cb)
        cb(ents, cons, pl);
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

Vec2d DesignSketchTool::snap_dir(const Vec2d& anchor, const Vec2d& raw, bool& locked) const
{
    locked = false;
    if (m_snap_off) return raw;

    const Vec2d d = raw - anchor;
    const double r = d.norm();
    if (r < 1e-9) return raw;

    const double tol_deg = 5.0;                 // inference half-window
    double ang = std::atan2(d.y(), d.x()) * 180.0 / M_PI;  // (-180,180]
    if (ang < 0.0) ang += 360.0;                            // [0,360)

    // Base angles within one quadrant, replicated every 90 deg up to 360.
    static const double base[] = {0.0, 30.0, 45.0, 60.0};
    double best_cand = ang, best_diff = 1e30;
    for (int q = 0; q < 4; ++q) {
        for (double b : base) {
            const double cand = b + 90.0 * q;
            double diff = std::abs(ang - cand);
            if (diff > 180.0) diff = 360.0 - diff;
            if (diff < best_diff) { best_diff = diff; best_cand = cand; }
        }
    }
    if (best_diff > tol_deg) return raw;

    locked = true;
    const double rad = best_cand * M_PI / 180.0;
    return anchor + r * Vec2d(std::cos(rad), std::sin(rad));
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

    // Committed entities of this session (selected ones drawn white).
    const ColorRGBA white(1.0f, 1.0f, 1.0f, 1.0f);
    std::vector<Vec2d> point_markers, sel_point_markers;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        const SketchEntity& e = m_entities[i];
        const bool selected =
            std::find(m_selection.begin(), m_selection.end(), int(i)) != m_selection.end();
        const ColorRGBA col = selected ? white : (e.construction ? grey : orange);
        if (e.type == SketchEntity::Type::Point) {
            (selected ? sel_point_markers : point_markers).push_back(e.p0);
            continue;
        }
        bool closed = false;
        std::vector<Vec2d> poly = entity_polyline(e, closed);
        draw_quad_strip(selected ? m_highlight_model : m_line_model, poly, closed, col);
    }
    if (!point_markers.empty())
        draw_vertices(m_vertex_model, point_markers, yellow);
    if (!sel_point_markers.empty())
        draw_vertices(m_highlight_model, sel_point_markers, white);

    // In-progress entity preview for the active tool.
    const ColorRGBA preview = m_construction ? grey : orange;
    switch (m_mode) {

    case Mode::Select:
        break;   // selection highlight is drawn above; no rubber-band preview

    case Mode::Polyline: {
        std::vector<Vec2d> pts = m_points;
        if (m_has_cursor)
            pts.push_back(m_cursor);
        draw_quad_strip(m_highlight_model, pts, false, preview);
        draw_vertices(m_vertex_model, m_points, yellow);
        // Teal rubber-band = the new segment is locked to an inference angle.
        if (m_cursor_locked && m_has_cursor && !m_points.empty()) {
            const ColorRGBA lock(0.10f, 0.85f, 0.80f, 1.0f);
            std::vector<Vec2d> seg = { m_points.back(), m_cursor };
            draw_quad_strip(m_line_model, seg, false, lock);
        }
        break;
    }

    case Mode::Line: {
        std::vector<Vec2d> pts = m_points;
        if (m_has_cursor && m_points.size() == 1)
            pts.push_back(m_cursor);
        if (pts.size() >= 2) {
            const ColorRGBA col = (m_cursor_locked && m_points.size() == 1)
                                      ? ColorRGBA(0.10f, 0.85f, 0.80f, 1.0f) : preview;
            draw_quad_strip(m_highlight_model, pts, false, col);
        }
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

// Adjacency endpoints for loop walking (Circles/Points have none -> stand-alone).
static void entity_endpoints(const SketchEntity& e, std::vector<Vec2d>& out)
{
    out.clear();
    if (e.type == SketchEntity::Type::Line) {
        out.push_back(e.p0);
        out.push_back(e.p1);
    } else if (e.type == SketchEntity::Type::Arc) {
        out.push_back(e.center + e.radius * Vec2d(std::cos(e.start_angle), std::sin(e.start_angle)));
        out.push_back(e.center + e.radius * Vec2d(std::cos(e.end_angle),   std::sin(e.end_angle)));
    }
}

// Reference point for positioning ops: a Point's position, or a Circle/Arc centre.
// Lines have no single reference point (return false).
static bool entity_ref_point(const SketchEntity& e, Vec2d& out)
{
    switch (e.type) {
    case SketchEntity::Type::Point:  out = e.p0;     return true;
    case SketchEntity::Type::Circle:
    case SketchEntity::Type::Arc:    out = e.center; return true;
    default:                         return false;   // Line
    }
}

// Rigidly translate a whole entity (keeps size/shape).
static void translate_entity(SketchEntity& e, const Vec2d& d)
{
    e.p0 += d;
    e.p1 += d;
    e.center += d;
}

int DesignSketchTool::hit_test(const Vec2d& p, double tol) const
{
    double best = tol;
    int bi = -1;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        const double d = entity_pick_dist(p, m_entities[i]);
        if (d < best) { best = d; bi = int(i); }
    }
    return bi;
}

std::vector<int> DesignSketchTool::connected_loop(int seed) const
{
    std::vector<int> out;
    if (seed < 0 || seed >= int(m_entities.size())) return out;
    const double eps2 = 1e-6;
    std::vector<bool> vis(m_entities.size(), false);
    std::vector<int> stack = { seed };
    vis[seed] = true;
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        std::vector<Vec2d> ce;
        entity_endpoints(m_entities[cur], ce);
        if (ce.empty()) continue;                 // circle/point: not part of a chain
        for (size_t j = 0; j < m_entities.size(); ++j) {
            if (vis[j]) continue;
            std::vector<Vec2d> je;
            entity_endpoints(m_entities[j], je);
            if (je.empty()) continue;
            bool adj = false;
            for (const Vec2d& a : ce)
                for (const Vec2d& b : je)
                    if ((a - b).squaredNorm() < eps2) { adj = true; break; }
            if (adj) { vis[j] = true; stack.push_back(int(j)); }
        }
    }
    return out;
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

    // Selection mode: click to pick an entity, Shift/Ctrl to extend, double-click
    // to grab the whole connected loop. Drag falls through so the camera can orbit.
    if (m_mode == Mode::Select) {
        const bool extend = evt.ShiftDown() || evt.ControlDown();
        if (evt.LeftDown() || evt.LeftDClick()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            // Zoom-aware tolerance: project a point 8 px away and measure in plane units.
            const Linef3 r2 = canvas.mouse_ray(Point(evt.GetX() + 8, evt.GetY()));
            const Vec2d p2 = m_plane.project(r2.a, r2.vector());
            const double tol = std::max(1e-3, (p2 - p).norm());
            const int hit = hit_test(p, tol);

            if (evt.LeftDClick() && hit >= 0) {
                if (!extend) m_selection.clear();
                for (int idx : connected_loop(hit))
                    if (std::find(m_selection.begin(), m_selection.end(), idx) == m_selection.end())
                        m_selection.push_back(idx);
            } else if (hit >= 0) {
                auto it = std::find(m_selection.begin(), m_selection.end(), hit);
                if (extend) {
                    if (it == m_selection.end()) m_selection.push_back(hit);
                    else m_selection.erase(it);     // toggle off
                } else {
                    m_selection.clear();
                    m_selection.push_back(hit);
                }
            } else if (!extend) {
                m_selection.clear();                // clicked empty space
            }
            if (on_selection_changed) on_selection_changed(int(m_selection.size()));
            return true;
        }
        if (evt.RightDown()) {
            clear_selection();
            return true;
        }
        return false;                               // let drag orbit the camera
    }

    if (evt.Moving()) {
        m_snap_off = evt.ShiftDown();
        screen_to_plane(canvas, evt, m_cursor);
        m_has_cursor = true;
        m_cursor_locked = false;
        const bool line_like = (m_mode == Mode::Polyline || m_mode == Mode::Line);
        if (line_like && !m_points.empty())
            m_cursor = snap_dir(m_points.back(), m_cursor, m_cursor_locked);
        if (on_cursor_metrics && line_like && !m_points.empty() && !m_awaiting_length) {
            const Vec2d d = m_cursor - m_points.back();
            on_cursor_metrics(d.norm(), std::atan2(d.y(), d.x()) * 180.0 / M_PI, m_cursor_locked);
        }
        return true;
    }

    switch (m_mode) {

    case Mode::Polyline: {
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            m_snap_off = evt.ShiftDown();
            if (near_first(p)) {              // closure tests the raw (un-snapped) point
                push_closed_lines(m_points);  // close the loop
                m_points.clear();
                return true;
            }
            if (!m_points.empty()) {
                bool lk = false;
                p = snap_dir(m_points.back(), p, lk);  // lock new segment to inference angle
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

    case Mode::Line: {
        if (m_awaiting_length)         // length dialog is open: ignore canvas input
            return true;
        if (evt.LeftDown()) {
            Vec2d p;
            screen_to_plane(canvas, evt, p);
            m_snap_off = evt.ShiftDown();
            if (m_points.empty()) {     // first click = anchor
                m_points.push_back(p);
                return true;
            }
            bool lk = false;
            p = snap_dir(m_points.back(), p, lk);
            m_points.push_back(p);      // second click completes the segment
            m_awaiting_length = true;
            if (on_segment_drawn) {
                const Vec2d d = m_points[1] - m_points[0];
                on_segment_drawn(d.norm(), std::atan2(d.y(), d.x()) * 180.0 / M_PI);
            } else {
                keep_segment_as_drawn();  // no handler: commit as drawn
            }
            return true;
        }
        if (evt.RightDown()) {          // abandon the in-progress anchor
            m_points.clear();
            m_has_cursor = false;
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
