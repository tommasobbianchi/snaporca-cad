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
    void begin(const SketchPlane& plane);
    void cancel();
    bool is_active() const { return m_active; }
    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas);
    void render(GLCanvas3D& canvas);

    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool near_first(const Vec2d& p) const;
    void commit();

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;
    Vec2d               m_cursor{0,0};
    bool                m_has_cursor{false};
    GLModel             m_line_model;
    GLModel             m_vertex_model;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignSketchTool_hpp_
