#ifndef slic3r_DesignSketchTool_hpp_
#define slic3r_DesignSketchTool_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/SketchEngine.hpp"
#include "libslic3r/SketchInference.hpp"
#include "libslic3r/SketchSolver.hpp"
#include "GLModel.hpp"
#include <functional>
#include <vector>
#include <string>
#include <utility>

class wxMouseEvent;
class wxPoint;

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
    enum class Mode { Select, Dimension, Polyline, Line, CornerRect, CenterRect, ObliqueRect,
                      RoundedRect, CenterCircle, TwoPointCircle, Point,
                      ThreePointCircle, ThreePointArc, TangentArc, CenterArc, Slot, ArcSlot, Polygon,
                      Ellipse, EllipseArc, BSpline,
                      Constrain };

    void begin(const SketchPlane& plane, Mode mode = Mode::Polyline);
    // Re-open a committed entity sketch for full in-canvas editing: load its entities +
    // driving constraints, re-detect the polygon/rect/slot grouping, and live-solve. The
    // caller re-commits via finish() (the panel replaces the feature, see m_edit_index).
    void begin_edit(const std::vector<SketchEntity>& entities,
                    const std::vector<SketchEntityConstraintDef>& constraints,
                    const SketchPlane& plane);
    void set_tool(Mode mode);                 // switch tool, keep accumulated entities
    void set_construction(bool c) { m_construction = c; }
    void set_polygon_sides(int n) { m_polygon_sides = (n < 3 ? 3 : n); }
    void set_polygon_circumscribed(bool c) { m_polygon_circumscribed = c; }
    void finish();                            // emit accumulated entities, end session
    void cancel();
    bool is_active() const { return m_active; }
    bool has_entities() const { return !m_entities.empty(); }
    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& canvas);
    void render(GLCanvas3D& canvas);

    // Persistent committed sketches to draw even when no session is active (e.g. an
    // un-consumed sketch left visible after its extrude is removed). Each carries its
    // own plane. render() draws these as translucent faces + outlines.
    struct DisplaySketch { std::vector<SketchEntity> entities; SketchPlane plane; };
    void set_display_sketches(std::vector<DisplaySketch> ds) { m_display_sketches = std::move(ds); }
    bool has_display() const { return m_active || !m_display_sketches.empty(); }

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
    // Constraint manager (C3.4): entity indices the panel asks to highlight (the
    // entities a selected constraint references); rendered yellow in Constrain mode.
    void set_constraint_highlight(std::vector<int> v) { m_constraint_hl = std::move(v); }
    // The committed feature's constraints, supplied so Constrain-mode render can draw
    // an iconic glyph badge per constraint near its primary entity (C3.4b).
    void set_constraint_glyphs(std::vector<SketchEntityConstraintDef> v) { m_constrain_cons = std::move(v); }

    // Line tool: after a single segment is placed, the panel pops a length dialog
    // (length, angle_deg are the as-drawn values); it then resolves via
    // apply_segment_length() (exact length) or keep_segment_as_drawn() (cancel).
    std::function<void(double length, double angle_deg)> on_segment_drawn;
    void apply_segment_length(double len);  // rescale the pending segment, then commit it
    void keep_segment_as_drawn();           // commit the pending segment unchanged

    // Live readout while drawing a Line/Polyline segment (anchor->cursor metrics).
    std::function<void(double length, double angle_deg, bool locked)> on_cursor_metrics;

    // DoF feedback (P3): solver state after each live solve. dof>0 = under-constrained,
    // dof==0 = fully constrained, ok==false = conflicting/inconsistent constraints.
    // has_constraints is false while the sketch carries no driving constraints yet.
    std::function<void(int dof, bool ok, bool has_constraints)> on_solve_state;

    // Selection (Mode::Select): pick points/lines/arcs/circles of the in-session
    // sketch; Shift/Ctrl extends, double-click grabs the whole connected loop.
    const std::vector<int>& selection() const { return m_selection; }
    void clear_selection();
    void delete_selected();                         // erase selected entities
    std::function<void(int count)> on_selection_changed;

    // Dimension tool: infer a driving dimension from the current selection and set
    // it exactly. Sizing: 1 line=Length, 1 circle=Diameter, 1 arc=Radius,
    // 2 lines=Angle. Positioning (a value of 0 makes them coincident):
    // 2 point-likes (point/circle-centre/arc-centre)=Distance, moving the 2nd onto
    // the 1st; a point-like + a line=DistanceToLine, moving the point-like's
    // reference point onto/away-from the line (e.g. a circle centre onto an axis).
    enum class DimType { None, Length, Diameter, Radius, Angle, Distance, DistanceToLine };
    DimType dimension_kind() const;     // what the selection supports (None if invalid)
    double  dimension_current() const;  // current value, to pre-fill the dialog
    void    apply_dimension(double v);  // set it exactly, then clear the selection

    // Onshape-style Dimension tool (Mode::Dimension): with the tool active you click
    // directly in the viewport — 2 points -> Distance, a line -> Length, a circle ->
    // Diameter, an arc -> Radius, a point then a line -> DistanceToLine. A quote line
    // with extension lines, arrowheads and a numeric label is PLACED in the sketch and
    // drives the geometry (auto-offset; label editable). on_dimension_pick_complete
    // fires when a pick resolves so the panel can pop the value card pre-filled.
    std::function<void(double current)> on_dimension_pick_complete;
    DimType pending_dimension_type() const;  // type of the dim awaiting a value, or None
    void    set_dimension_value(double v);   // apply the typed value to the placed dim
    void    cancel_dimension_value();        // keep the placed dim at its measured value

    // Onshape-style in-canvas value editing: open a floating text editor at the given
    // screen pixel, pre-filled with `current`; commit applies the value, cancel keeps
    // it. The owner (DesignCanvas) hosts the wxTextCtrl over the GL canvas. This is the
    // single numeric-entry path for all sketch dimensions (replaces the modal cards).
    std::function<void(wxPoint screen_px, double current,
                       std::function<void(double)> commit,
                       std::function<void()> cancel)> on_inline_edit;

    // Driving dimension constraints accumulated during the session (the Dimension
    // tool records a SketchEntityConstraintDef per applied dimension); committed
    // alongside the entities on finish() so the kernel keeps enforcing them.
    const std::vector<SketchEntityConstraintDef>& constraints() const { return m_constraints; }

    // Emitted by finish() with the accumulated entities + driving constraints.
    std::function<void(const std::vector<SketchEntity>&,
                       const std::vector<SketchEntityConstraintDef>&,
                       const SketchPlane&)> on_commit_entities;
    // Legacy single-profile commit (kept for compatibility; unused by entity tools).
    std::function<void(const SketchProfile&, const SketchPlane&)> on_commit;
    // Emitted when a closed-loop face is clicked in Select mode (Onshape: a region
    // becomes a selectable face → extrude). The panel commits the sketch + extrudes.
    std::function<void()> on_face_selected;
    // Esc pressed while the tool is active: exit/cancel the session (the panel restores
    // Feature mode). Layered: an in-progress entity or a non-Select draw tool is dropped
    // first; a second Esc exits the session.
    std::function<void()> on_exit;
    void request_exit();

private:
    bool screen_to_plane(GLCanvas3D& canvas, const wxMouseEvent& evt, Vec2d& out) const;
    bool near_first(const Vec2d& p) const;

    // Onshape-style angle inference: snap the direction anchor->raw to the nearest
    // of {0,30,45,60,90} deg (replicated every 90 deg) when within tolerance, keeping
    // the same length. Sets `locked` when a snap was applied. Suppressed by m_snap_off.
    Vec2d snap_dir(const Vec2d& anchor, const Vec2d& raw, bool& locked) const;
    // Snap a placed point onto the nearest existing entity endpoint within ~8 px so
    // chains join across entities (a line + an arc can close into one loop). Shift
    // disables it. `snapped` reports whether a vertex was hit.
    Vec2d snap_vertex(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& raw, bool& snapped) const;

    // --- P1 inference / auto-constraint engine ---------------------------------
    // Plane-units tolerance equivalent to ~`px` screen pixels at the cursor.
    double screen_tol(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& at, double px = 8.0) const;
    // Run kernel inference at the cursor, cache the target for the hint renderer.
    InferenceSnap infer_at(GLCanvas3D& canvas, const wxMouseEvent& evt, const Vec2d& raw) const;
    // True if m_constraints already holds an equivalent Coincident between the two refs.
    bool has_coincident(int ea, SketchPointRole ra, int eb, SketchPointRole rb) const;
    // Append candidates, live-solve, and roll back the batch if it turns the system
    // inconsistent. Returns true when the batch was kept.
    bool try_add_constraints(const std::vector<SketchEntityConstraintDef>& cands);
    // After entities [base, end) were committed, auto-emit the constraints that make
    // the new geometry stick: Coincident between co-located endpoints (so loops close
    // on their own) and Horizontal/Vertical on axis-aligned new segments.
    void infer_auto_constraints(int base);

    // Selection helpers (Mode::Select).
    int hit_test(const Vec2d& p, double tol) const;       // nearest entity within tol, or -1
    std::vector<int> connected_loop(int seed) const;      // entities joined by shared endpoints
    void apply_angle_between(int ia, int ib, double deg); // rotate line B to set the A^B angle
    bool selection_valid() const;                         // all selection indices in range
    void record_dimension_constraint(double v);           // append the driving def for the selection
    void resolve_live();                                  // solve accumulated constraints on m_entities now
    // Drag-aware re-solve: pins the dragged point at its current coord and lets the
    // solver move the rest (Slvs dragged[]). Used live while a point grab is active.
    void resolve_live_drag(int dragged_ei, SketchPointRole dragged_role);

    // Placed dimension annotation. References entity points/entities (not cached
    // coords) so the quote follows the geometry as the kernel solves it. `value`
    // drives the constraint stored at index `con` in m_constraints.
    struct DimAnnot {
        DimType         kind{DimType::None};
        int             ea{-1}; SketchPointRole ra{SketchPointRole::P0};
        int             eb{-1}; SketchPointRole rb{SketchPointRole::P0};
        double          value{0.0};
        double          side{1.0};   // perpendicular offset sign of the quote line
        int             con{-1};     // slot in m_constraints driving this dimension
        Vec2d           label_pos{0, 0};  // cached label centre (plane coords), for picking
    };

    // --- Onshape-style visual editing: handles + parametric feature grouping -----
    // A draggable handle on a defining point of an entity (or a derived point of a
    // feature group). GUI-only; recomputed from solved geometry every frame (never
    // persisted), so handles always track the current solve. Derived roles (radius,
    // slot width/centres, rect corners, polygon vertex, ellipse axes) let tools that
    // decompose into raw Line/Arc entities still expose their parametric controls.
    enum class HandleRole { P0, P1, Center, RadiusHandle,
                            SlotCenter0, SlotCenter1, SlotWidth,
                            RectCorner, PolygonVertex, MajorAxis, MinorAxis, BSplineCtrl };
    struct Handle {
        HandleRole role{HandleRole::P0};
        int   ei{-1};          // primary entity index
        int   group{-1};       // index into m_features, or -1 for a raw-entity handle
        int   ctrl_index{-1};  // BSplineCtrl pole index
        Vec2d pos{0, 0};       // current plane coords (recomputed each frame)
        bool  hovered{false};
    };
    // A parametric grouping over a contiguous run of entities produced by one gesture.
    // Slot/Rect/Polygon/etc. have no SketchEntity type of their own — they decompose
    // into raw Line/Arc entities — so the Feature carries the gesture's anchors so
    // derived handles + characteristic dimensions can be reconstructed.
    enum class FeatureKind { Free, Line, Circle, Arc, CornerRect, CenterRect,
                             Slot, ArcSlot, Polygon, Ellipse, RoundedRect, BSpline };
    struct Feature {
        FeatureKind kind{FeatureKind::Free};
        int    begin{0}, end{0};   // [begin,end) into m_entities
        Vec2d  c0{0, 0}, c1{0, 0}; // slot centres / rect corners / ellipse centre+major
        double param{0.0};         // slot half-width / polygon circumradius / fillet radius
        int    sides{0};           // polygon side count
    };
    // Build the live handle set for the current selection / just-drawn feature.
    std::vector<Handle> build_handles() const;
    // Nearest handle to plane-point p within tol; fills `out`. (Phase A: stub.)
    bool hit_test_handle(const Vec2d& p, double tol, Handle& out) const;
    // Move a handle to `target`, applying the role-specific geometry edit + re-solve.
    void set_handle(const Handle& h, const Vec2d& target);
    // On a no-button move, recompute the hovered handle; returns true iff it changed
    // (so the caller forces exactly one repaint). No-op for non-Moving events.
    bool update_hover(GLCanvas3D& canvas, wxMouseEvent& evt);
    // Index of the Feature whose [begin,end) entity span contains ei, or -1.
    int  feature_of(int ei) const;
    // Re-detect parametric Feature groups (polygon / rect / slot) from the raw entity
    // list — used when a committed sketch is re-opened, where m_features is empty.
    void rebuild_features_from_entities();
    // Open/close a Feature record around the entities a single gesture appends.
    void begin_feature(FeatureKind kind);
    void end_feature(const Vec2d& c0 = Vec2d(0, 0), const Vec2d& c1 = Vec2d(0, 0),
                     double param = 0.0, int sides = 0);

    bool point_at(int ei, SketchPointRole role, Vec2d& out) const;          // current coords
    void set_point(int ei, SketchPointRole role, const Vec2d& v);           // move an entity point
    bool hit_test_point(const Vec2d& p, double tol, int& ei, SketchPointRole& role) const;
    int  hit_test_dimension(const Vec2d& p, double tol) const;              // nearest dim label
    void edit_dimension(int di);                                            // reopen value card for di
    // Representative plane-coords anchor of a dimension (label centre if known, else a
    // geometric midpoint/centre) — where the in-canvas value editor is positioned.
    Vec2d dim_anchor(const DimAnnot& a) const;
    // Open the in-canvas value editor on dimension `di` (falls back to the modal
    // pick-complete callback when no inline-edit host is wired).
    void open_value_editor(int di);
    // In-canvas editor for a line's angle-to-horizontal; commit rotates the segment
    // geometrically about P0 (no single-line angle constraint in libslvs).
    void open_angle_editor(int ei);
    void set_line_angle(int ei, double deg);
    // In-canvas editors for a regular polygon's side length and orientation. Both edit
    // the whole loop GEOMETRICALLY (polygon has no centre entity): side scales it
    // uniformly about its centre, angle rotates it. set_polygon_radius is the shared
    // uniform-scale primitive (circumradius).
    void open_polygon_side_editor(int fi);
    void open_polygon_angle_editor(int fi);
    void set_polygon_side(int fi, double side);
    void set_polygon_angle(int fi, double deg);
    void set_polygon_radius(int fi, double R);
    // Arc sweep-angle quote: geometric edit (SLVS angle is line-to-line only). Keeps the
    // arc start point + radius fixed and moves the end point to span `deg` degrees.
    void open_arc_angle_editor(int ei);
    void set_arc_sweep(int ei, double deg);
    // Arc handle drag (3 grips): Center rigidly translates; the START point changes the
    // radius (keeps both sweep angles); the END point changes the sweep angle (keeps the
    // radius). Geometric — no solver (SLVS has no arc radius/angle handle concept here).
    void drag_arc_handle(int ei, SketchPointRole role, const Vec2d& target);
    // Ellipse axis labels (geometric edit of the semi-axes a/b; phi via the major grip).
    void open_ellipse_axis_editor(int ei, bool major);
    void set_ellipse_axis(int ei, bool major, double v);
    // Drop orientation constraints (H/V/Parallel/Perp/Angle/LockX/LockY) on entities in
    // [begin,end). A ROTATION makes inferred per-edge H/V inconsistent, so re-solving
    // against them collapses the shape — drop them first (fixes up DimAnnot.con indices).
    void drop_orientation_constraints(int begin, int end);
    // Drag a polygon vertex while keeping the loop REGULAR: scale + rotate the whole
    // polygon about its centroid so the grabbed vertex follows `target` (adjusts
    // circumradius + orientation together).
    void drag_polygon_vertex(int fi, int ei, SketchPointRole role, const Vec2d& target);
    double measure_dim(const DimAnnot& a) const;                            // value from geometry
    SketchEntityConstraintDef constraint_for(const DimAnnot& a) const;      // driving def
    int  place_dimension(DimAnnot a);                                       // create+drive+notify
    std::string dim_text(const DimAnnot& a) const;                          // rendered label string
    void render_dimensions(double unit_per_px);                            // quote lines + labels
    // Draw ONE dimension's quote (extension/dimension lines, arrowheads, label) and
    // return its label centre in out_label; false if the annot can't be drawn. Shared
    // by render_dimensions (placed driving quotes) and render_live_quotes (live ones).
    bool draw_dim_quote(const DimAnnot& a, double th, const ColorRGBA& col, Vec2d& out_label);
    // Live, non-driving characteristic quotes for the entity being edited (point/handle
    // drag, or a lone selection): the tool's defining dimensions shown Onshape-style so
    // editing shows live values; click one (m_live_quotes) to promote it to a driving
    // dim. Self-gates; skips a dim already driven on that entity.
    void render_live_quotes(double unit_per_px);
    // Iconic constraint badges (C3.4b): for each m_constrain_cons entry, append a
    // small screen-constant glyph (H, V, ∥, ⊥, =, ○, …) near its primary entity into
    // `out`; glyphs touching the same entity stack so they don't overlap.
    void build_constraint_glyphs(double unit_per_px, std::vector<std::pair<Vec2d, Vec2d>>& out) const;
    void draw_strokes(GLModel& model, const std::vector<std::pair<Vec2d, Vec2d>>& segs,
                      double hw, const ColorRGBA& color);
    void draw_text(GLModel& model, const std::string& s, const Vec2d& center,
                   double height, const ColorRGBA& color);                  // GL stroke font

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
    // Center-start-end arc: click center, then start (sets radius), then a third
    // point whose direction from the center sets the CCW end angle.
    std::vector<SketchEntity> make_center_arc(const Vec2d& center, const Vec2d& start, const Vec2d& end_dir) const;
    std::vector<SketchEntity> make_slot(const Vec2d& c0, const Vec2d& c1, double half_width) const;
    std::vector<SketchEntity> make_arc_slot(const Vec2d& center, const Vec2d& start,
                                            const Vec2d& end_dir, double half_width) const;
    std::vector<SketchEntity> make_rounded_rect(const Vec2d& a, const Vec2d& b, const Vec2d& radius_pt) const;
    std::vector<SketchEntity> make_polygon(const Vec2d& center, const Vec2d& vertex, int sides) const;
    // Ellipse: click center, then major-axis endpoint (sets a + rotation phi),
    // then a point whose perpendicular distance to the major axis sets b.
    std::vector<SketchEntity> make_ellipse(const Vec2d& center, const Vec2d& major_end,
                                           const Vec2d& minor_pt) const;
    // Elliptical arc: same 3 axis clicks, then start and end points whose parametric
    // angles on the ellipse bound the CCW sweep.
    std::vector<SketchEntity> make_bspline(const std::vector<Vec2d>& ctrl) const;
    std::vector<SketchEntity> make_ellipse_arc(const Vec2d& center, const Vec2d& major_end,
                                               const Vec2d& minor_pt, const Vec2d& start_pt,
                                               const Vec2d& end_pt) const;
    void append_entities(const std::vector<SketchEntity>& ents);
    void draw_entities_preview(const std::vector<SketchEntity>& ents, const ColorRGBA& color);

    // Sample an entity into a 2D polyline for the overlay renderer.
    std::vector<Vec2d> entity_polyline(const SketchEntity& e, bool& closed) const;

    // Closed regions formed by the current (non-construction) entities: each a CCW-
    // ordered boundary polygon on the plane. A circle is its own region; line/arc
    // chains are walked endpoint-to-endpoint into loops. Used to fill faces.
    std::vector<std::vector<Vec2d>> closed_regions() const;
    std::vector<std::vector<Vec2d>> closed_regions(const std::vector<SketchEntity>& ents) const;
    // Index of the closed region containing plane-point p (point-in-polygon), or -1.
    int region_at(const Vec2d& p) const;

    void draw_quad_strip(GLModel& model, const std::vector<Vec2d>& pts, bool closed, const ColorRGBA& color);
    // half_size is the square marker half-extent in PLANE units. Callers pass a
    // zoom-scaled value (k / zoom) for screen-constant handles; the default keeps
    // legacy point markers exactly as before.
    void draw_vertices(GLModel& model, const std::vector<Vec2d>& pts, const ColorRGBA& color,
                       double half_size = 1.3);
    void draw_fill(GLModel& model, const std::vector<Vec2d>& poly, const ColorRGBA& color);

    bool                m_active{false};
    SketchPlane         m_plane;
    std::vector<Vec2d>  m_points;       // clicks of the in-progress entity / chain
    std::vector<SketchEntity> m_entities; // committed entities of this session
    bool                m_construction{false};
    int                 m_polygon_sides{6};
    bool                m_polygon_circumscribed{false};
    Vec2d               m_cursor{0,0};
    bool                m_has_cursor{false};
    bool                m_snap_off{false};      // Shift held -> suppress angle snapping
    InferenceSnap       m_cursor_snap;          // last cursor inference target (for hint render)
    bool                m_cursor_locked{false}; // rubber-band segment is angle-locked
    bool                m_awaiting_length{false}; // Line tool: length dialog is open
    std::vector<int>    m_selection;              // selected entity indices (Mode::Select)
    std::vector<std::pair<int, SketchPointRole>> m_point_sel;  // selected individual points
    int                 m_last_mouse_x{0};        // last cursor pos (canvas client px), for
    int                 m_last_mouse_y{0};        // anchoring the in-canvas value editor
    bool                m_dragging_point{false};  // a point grab is in progress (Mode::Select)
    int                 m_drag_ei{-1};            // entity whose point is being dragged
    int                 m_drag_poly_fi{-1};       // >=0 if the grabbed point is a polygon
                                                  // vertex: drag scales+rotates the loop
    SketchPointRole     m_drag_role{SketchPointRole::P0};
    std::vector<SketchEntityConstraintDef> m_constraints; // driving dims, committed on finish

    // Onshape-style visual editing state.
    bool                  m_show_handles{false};   // draw + interact with handles
    bool                  m_dragging_handle{false};// a handle grab is in progress
    Handle                m_drag_handle;           // the handle being dragged
    bool                  m_has_hover_handle{false};// cursor is near a handle (highlight it)
    Handle                m_hover_handle;          // the hovered handle (recomputed on move)
    std::vector<DimAnnot> m_live_quotes;           // live non-driving characteristic quotes,
                                                   // clickable to promote to driving dims
    Vec2d                 m_live_poly_side_label{0,0};  // polygon side-length quote label
    Vec2d                 m_live_poly_angle_label{0,0}; // polygon orientation quote label
    int                   m_live_poly_fi{-1};           // their Feature (geometric edits)
    Vec2d                 m_live_arc_angle_label{0,0};  // arc sweep-angle quote label
    int                   m_live_arc_ei{-1};            // the arc it belongs to (geometric edit)
    Vec2d                 m_live_ellipse_major_label{0,0}; // ellipse semi-major quote label
    Vec2d                 m_live_ellipse_minor_label{0,0}; // ellipse semi-minor quote label
    int                   m_live_ellipse_ei{-1};        // the ellipse the labels belong to
    std::vector<Feature>  m_features;              // parametric groups over m_entities
    int                   m_open_feature{-1};      // index of the Feature being built, or -1

    // DoF feedback state, refreshed by resolve_live() from the libslvs solve result.
    int               m_dof{-1};          // remaining DoF; 0 = fully constrained, <0 = unknown
    bool              m_solve_ok{true};   // solver consistent (no conflicting constraints)
    std::vector<char> m_entity_conflict;  // per-entity flag: touched by a conflicting constraint
    std::vector<DimAnnot> m_dimensions;           // placed dimension quotes (Mode::Dimension)
    int                 m_dim_e0{-1};             // first picked point's entity (Dimension)
    SketchPointRole     m_dim_r0{SketchPointRole::P0};
    bool                m_dim_has0{false};        // a first point is pending
    int                 m_pending_dim{-1};        // dim awaiting a value-card entry
    Mode                m_mode{Mode::Polyline};
    int                 m_sel_a{-1};   // picked segment endpoints (legacy Constrain mode)
    int                 m_sel_b{-1};
    bool                m_constrain_entities{false}; // Constrain mode acts on entities
    int                 m_pick0{-1};   // picked line-entity indices (entity Constrain)
    int                 m_pick1{-1};
    int                 m_pick2{-1};   // third slot (Symmetric axis)
    Vec2d               m_pick0_pt{0,0}; // plane-coords of the slot-0 pick (trim/extend)
    std::vector<int>    m_constraint_hl; // entities highlighted by the constraint manager
    std::vector<SketchEntityConstraintDef> m_constrain_cons; // for glyph badges (C3.4b)
    GLModel             m_line_model;
    GLModel             m_vertex_model;
    GLModel             m_highlight_model;
    GLModel             m_fill_model;       // translucent face fill for closed regions
    std::vector<DisplaySketch> m_display_sketches;  // committed sketches drawn persistently
};

}} // namespace Slic3r::GUI

#endif // slic3r_DesignSketchTool_hpp_
