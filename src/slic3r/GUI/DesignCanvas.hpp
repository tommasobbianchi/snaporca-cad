#ifndef slic3r_DesignCanvas_hpp_
#define slic3r_DesignCanvas_hpp_

#include <wx/panel.h>

#include <functional>
#include <string>

#include "3DBed.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SketchEngine.hpp"
#include "DesignSketchTool.hpp"

class wxGLCanvas;

namespace Slic3r {

class TriangleMesh;

namespace GUI {

class GLCanvas3D;

class DesignCanvas : public wxPanel
{
public:
    explicit DesignCanvas(wxWindow* parent);
    ~DesignCanvas() override;

    void set_mesh(const TriangleMesh& mesh);
    void clear_mesh();

    void set_preview_mesh(const TriangleMesh& mesh);
    void clear_preview();

    void fit_view();
    void set_view(const std::string& view_name);

    void begin_sketch(const SketchPlane& plane, DesignSketchTool::Mode mode);
    bool is_sketching() const;
    void cancel_sketch();
    void set_on_sketch_commit(std::function<void(const SketchProfile&, const SketchPlane&)> cb);

private:
    void reload(bool keep_view);

    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};
    Bed3D       m_bed;
    Model       m_model;
    bool        m_first_frame{true};

    DesignSketchTool m_sketch_tool;
    std::function<void(const SketchProfile&, const SketchPlane&)> m_on_sketch_commit;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignCanvas_hpp_
