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

class DesignSketchTool {
public:
    enum class Mode { Polyline, Rectangle, Circle, Constrain };

    void begin(const SketchPlane& plane, Mode mode = Mode::Polyline);
    void cancel();
    bool is_active() const { return m_active; }
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

    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool near_first(const Vec2d& p) const;
    void commit();

    void draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color);
    void draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color);

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;
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
