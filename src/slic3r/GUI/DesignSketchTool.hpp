#ifndef slic3r_DesignSketchTool_hpp_
#define slic3r_DesignSketchTool_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/SketchEngine.hpp"
#include "GLModel.hpp"
#include <functional>
#include <vector>

class wxMouseEvent;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

// Onshape-style sketch session. `begin` enters a session on a plane; the active
// drawing tool (Mode) can be switched mid-session via `set_tool` while entities
// accumulate. `finish` commits the whole entity list as one sketch feature;
// `cancel` aborts. Constrain is a separate legacy mode that operates on a
// committed profile's points (entity constraints land in a later chunk).
class DesignSketchTool {
public:
    enum class Mode { Polyline, CornerRect, CenterRect, CenterCircle, Point,
                      ThreePointCircle, ThreePointArc, TangentArc, Slot, Polygon,
                      Constrain };

    void begin(const SketchPlane& plane, Mode mode = Mode::Polyline);
    void set_tool(Mode mode);                 // switch tool, keep accumulated entities
    void set_construction(bool c) { m_construction = c; }
    void set_polygon_sides(int n) { m_polygon_sides = (n < 3 ? 3 : n); }
    void finish();                            // emit accumulated entities, end session
    void cancel();
    bool is_active() const { return m_active; }
    bool has_entities() const { return !m_entities.empty(); }
    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas);
    void render(GLCanvas3D& canvas);

    // Constrain mode: load an already-committed profile for entity picking +
    // constraint application (the geometry is solved in the kernel, not here).
    void begin_constrain(const SketchProfile& prof, const SketchPlane& plane);
    bool is_constraining() const { return m_active && m_mode == Mode::Constrain; }
    // Replace the displayed profile (e.g. after the kernel re-solved it).
    void set_profile_points(const std::vector<Vec2d>& pts) { m_points = pts; }
    // The currently picked segment's endpoint indices into the profile.
    bool selected_segment(int& a, int& b) const;

    // Entity-aware Constrain (Fase 4.2): load a committed entity sketch and pick
    // Line entities (constraints are solved against entity endpoints in the kernel).
    void begin_constrain_entities(const std::vector<SketchEntity>& ents, const SketchPlane& plane);
    bool is_constraining_entities() const { return m_active && m_mode == Mode::Constrain && m_constrain_entities; }
    // Up to two picked line-entity indices; returns true if at least one is picked.
    bool selected_constrain_entities(int& e0, int& e1) const { e0 = m_pick0; e1 = m_pick1; return m_pick0 >= 0; }
    // Third pick slot (Symmetric axis): only filled after slots 0 and 1 are set.
    int  pick2() const { return m_pick2; }
    // Plane-coords of the click that filled slot 0 (for pick-point edit ops: trim/extend).
    bool pick0_point(Vec2d& out) const { out = m_pick0_pt; return m_pick0 >= 0; }
    // Refresh the displayed entities after the kernel re-solved them.
    void set_constrain_entities(const std::vector<SketchEntity>& ents) { m_entities = ents; }

    // Emitted by finish() with the accumulated entities (Onshape multi-entity path).
    std::function<void(const std::vector<SketchEntity>&, const SketchPlane&)> on_commit_entities;
    // Legacy single-profile commit (kept for compatibility; unused by entity tools).
    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool near_first(const Vec2d& p) const;

    // Onshape-style angle inference: snap the direction anchor->raw to the nearest
    // of {0,30,45,60,90} deg (replicated every 90 deg) when within tolerance, keeping
    // the same length. Sets `locked` when a snap was applied. Suppressed by m_snap_off.
    Vec2d snap_dir(const Vec2d& anchor, const Vec2d& raw, bool& locked) const;

    // Entity builders: append to m_entities (honoring the construction flag).
    void push_line(const Vec2d& a, const Vec2d& b);
    void push_closed_lines(const std::vector<Vec2d>& corners);
    void push_open_chain(const std::vector<Vec2d>& pts);
    void push_circle(const Vec2d& center, double radius);
    void push_point(const Vec2d& p);

    // Multi-click tool builders: return the entities for a finished gesture so
    // both on_mouse (append) and render (preview) share one geometry path.
    std::vector<SketchEntity> make_three_point_circle(const Vec2d& a, const Vec2d& b, const Vec2d& c) const;
    std::vector<SketchEntity> make_three_point_arc(const Vec2d& start, const Vec2d& end, const Vec2d& on_arc) const;
    std::vector<SketchEntity> make_tangent_arc(const Vec2d& start, const Vec2d& end) const;
    std::vector<SketchEntity> make_slot(const Vec2d& c0, const Vec2d& c1, double half_width) const;
    std::vector<SketchEntity> make_polygon(const Vec2d& center, const Vec2d& vertex, int sides) const;
    void append_entities(const std::vector<SketchEntity>& ents);
    void draw_entities_preview(const std::vector<SketchEntity>& ents, const ColorRGBA& color);

    // Sample an entity into a 2D polyline for the overlay renderer.
    std::vector<Vec2d> entity_polyline(const SketchEntity& e, bool& closed) const;

    void draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color);
    void draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color);

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;       // clicks of the in-progress entity / chain
    std::vector<SketchEntity> m_entities; // committed entities of this session
    bool                m_construction{false};
    int                 m_polygon_sides{6};
    Vec2d               m_cursor{0,0};
    bool                m_has_cursor{false};
    bool                m_snap_off{false};      // Shift held -> suppress angle snapping
    bool                m_cursor_locked{false}; // rubber-band segment is angle-locked
    Mode                m_mode{Mode::Polyline};
    int                 m_sel_a{-1};   // picked segment endpoints (legacy Constrain mode)
    int                 m_sel_b{-1};
    bool                m_constrain_entities{false}; // Constrain mode acts on entities
    int                 m_pick0{-1};   // picked line-entity indices (entity Constrain)
    int                 m_pick1{-1};
    int                 m_pick2{-1};   // third slot (Symmetric axis)
    Vec2d               m_pick0_pt{0,0}; // plane-coords of the slot-0 pick (trim/extend)
    GLModel             m_line_model;
    GLModel             m_vertex_model;
    GLModel             m_highlight_model;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignSketchTool_hpp_
