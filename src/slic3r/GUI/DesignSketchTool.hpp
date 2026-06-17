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
    enum class Mode { Polyline, CornerRect, CenterRect, CenterCircle, Point, Constrain };

    void begin(const SketchPlane& plane, Mode mode = Mode::Polyline);
    void set_tool(Mode mode);                 // switch tool, keep accumulated entities
    void set_construction(bool c) { m_construction = c; }
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

    // Emitted by finish() with the accumulated entities (Onshape multi-entity path).
    std::function<void(const std::vector<SketchEntity>&, const SketchPlane&)> on_commit_entities;
    // Legacy single-profile commit (kept for compatibility; unused by entity tools).
    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool near_first(const Vec2d& p) const;

    // Entity builders: append to m_entities (honoring the construction flag).
    void push_line(const Vec2d& a, const Vec2d& b);
    void push_closed_lines(const std::vector<Vec2d>& corners);
    void push_open_chain(const std::vector<Vec2d>& pts);
    void push_circle(const Vec2d& center, double radius);
    void push_point(const Vec2d& p);

    // Sample an entity into a 2D polyline for the overlay renderer.
    std::vector<Vec2d> entity_polyline(const SketchEntity& e, bool& closed) const;

    void draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color);
    void draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color);

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;       // clicks of the in-progress entity / chain
    std::vector<SketchEntity> m_entities; // committed entities of this session
    bool                m_construction{false};
    Vec2d               m_cursor{0,0};
    bool                m_has_cursor{false};
    Mode                m_mode{Mode::Polyline};
    int                 m_sel_a{-1};   // picked segment endpoints (Constrain mode)
    int                 m_sel_b{-1};
    GLModel             m_line_model;
    GLModel             m_vertex_model;
    GLModel             m_highlight_model;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignSketchTool_hpp_
