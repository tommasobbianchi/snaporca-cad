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
    void set_sketch_tool(DesignSketchTool::Mode mode);
    void set_sketch_construction(bool c);
    void set_sketch_polygon_sides(int n);
    void finish_sketch();
    bool is_sketching() const;
    void cancel_sketch();
    void set_on_sketch_commit(std::function<void(const SketchProfile&, const SketchPlane&)> cb);
    void set_on_sketch_entities_commit(
        std::function<void(const std::vector<SketchEntity>&, const SketchPlane&)> cb);

    // Constrain mode: load a committed profile for picking + constraint editing.
    void begin_constrain(const SketchProfile& prof, const SketchPlane& plane);
    // Leave constrain mode and clear any picked-entity highlight from the overlay.
    void end_constrain();
    bool is_constraining() const;
    bool selected_segment(int& a, int& b) const;
    void update_constrain_profile(const std::vector<Vec2d>& pts);

    // Entity-aware Constrain (Fase 4.2): pick Line entities of a committed sketch.
    void begin_constrain_entities(const std::vector<SketchEntity>& ents, const SketchPlane& plane);
    bool is_constraining_entities() const;
    bool selected_constrain_entities(int& e0, int& e1) const;
    bool pick0_point(Vec2d& out) const;   // plane-coords of the slot-0 pick (trim/extend)
    void update_constrain_entities(const std::vector<SketchEntity>& ents);

private:
    void reload(bool keep_view);

    wxGLCanvas* m_canvas_widget{nullptr};
    GLCanvas3D* m_canvas{nullptr};
    Bed3D       m_bed;
    Model       m_model;
    bool        m_first_frame{true};

    DesignSketchTool m_sketch_tool;
    std::function<void(const SketchProfile&, const SketchPlane&)> m_on_sketch_commit;
    std::function<void(const std::vector<SketchEntity>&, const SketchPlane&)> m_on_sketch_entities_commit;
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignCanvas_hpp_
