#ifndef slic3r_DesignViewport_hpp_
#define slic3r_DesignViewport_hpp_

#include <wx/panel.h>

#include <string>

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

class wxGLCanvas;
class wxGLContext;

namespace Slic3r { namespace GUI {

// Lightweight live 3D viewport embedded in the Design tab. It renders the current
// CadDocument body (a TriangleMesh) using the application's shared GL context and
// the "gouraud_light" shader, with orbit (left-drag) and zoom (wheel). This is a
// minimal preview canvas, deliberately NOT the full GLCanvas3D scene.
class DesignViewport : public wxPanel
{
public:
    explicit DesignViewport(wxWindow* parent);
    ~DesignViewport() override;

    // Replace the displayed body and reframe the camera on the next paint.
    // Safe to call at any time: the GPU upload is deferred to render() so it
    // always happens while the GL context is current.
    void set_mesh(const TriangleMesh& mesh);
    void clear_mesh();

    // Translucent "ghost" of a candidate feature, rendered over the committed body
    // without committing it (the Onshape-style preview). clear_preview() removes it.
    void set_preview_mesh(const TriangleMesh& mesh);
    void clear_preview();

    // Re-frame the camera on the current geometry (the "fit view" / reset-view
    // action). Reuses render()'s first-geometry framing by re-arming the flag.
    // fit_view keeps the current orientation; set_view snaps to a named standard
    // view ("iso"/"front"/"rear"/"left"/"right"/"top"/"bottom") then fits.
    void fit_view();
    void set_view(const std::string& view_name);

private:
    void on_paint(wxPaintEvent& evt);
    void on_size(wxSizeEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void render();

    wxGLCanvas*   m_canvas{nullptr};
    wxGLContext*  m_context{nullptr};
    GLModel       m_model;
    Camera        m_camera;
    BoundingBoxf3 m_bbox;

    TriangleMesh  m_pending_mesh;
    bool          m_mesh_dirty{false};
    bool          m_has_mesh{false};
    bool          m_camera_framed{false};
    // Next framing applies this named view when m_apply_named_view is set; Fit view
    // leaves orientation alone (m_apply_named_view=false) and only zoom-fits.
    std::string   m_view_request{"iso"};
    bool          m_apply_named_view{true};

    GLModel       m_preview_model;
    TriangleMesh  m_pending_preview;
    BoundingBoxf3 m_preview_bbox;
    bool          m_preview_dirty{false};
    bool          m_has_preview{false};

    bool          m_dragging{false};
    Vec2d         m_last_mouse{Vec2d::Zero()};
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignViewport_hpp_
