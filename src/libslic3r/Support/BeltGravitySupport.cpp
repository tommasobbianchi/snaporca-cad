#include "BeltFloorContext.hpp"
#include "SupportCommon.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"

#include "../BeltSliceStrategy.hpp"
#include "../BeltTransform.hpp"
#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Model.hpp"
#include "../PrintConfig.hpp"
#include "../Print.hpp"
#include "../TriangleMesh.hpp"
#include "../TriangleMeshSlicer.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r {

bool PrintObject::_generate_belt_gravity_support()
{
    const PrintConfig &pcfg = this->print()->config();

    // ---- 0. Guards ----
    if (!m_config.enable_support.value)
        return false;
    if (m_layers.empty())
        return false;
    if (!this->model_object())
        return false;
    {
        bool has_part = false;
        for (const ModelVolume *mv : this->model_object()->volumes)
            if (mv->is_model_part()) { has_part = true; break; }
        if (!has_part)
            return false;
    }

    const double top_gap = m_config.support_top_z_distance.value;
    double threshold_deg = m_config.support_threshold_angle.value;
    if (threshold_deg <= 0.)
        threshold_deg = 30.0;
    const double threshold_rad = Geometry::deg2rad(threshold_deg);
    const double cos_threshold = std::cos(threshold_rad);

    // ---- 1. World-frame mesh (upright; bed/belt = z=0; gravity = -Z) ----
    //
    // The base object transformation is the SAME one used by the model slicing
    // path before BeltSliceStrategy::apply_preslice_transforms modifies it.
    // In PrintObjectSlice.cpp:1427, slice_volumes() passes this->trafo_centered()
    // as the 'object_trafo' argument to slice_volumes_inner(), which then
    // sets params_base.trafo = object_trafo at line 175 and calls
    // apply_preslice_transforms(params_base.trafo, ...) at line 179.
    // Therefore T_world = trafo_centered() is the exact base transformation.
    const Transform3d T_world = this->trafo_centered();

    indexed_triangle_set world_mesh;
    for (const ModelVolume *mv : this->model_object()->volumes) {
        if (!mv->is_model_part())
            continue;
        indexed_triangle_set vol_its = mv->mesh().its;
        its_transform(vol_its, T_world * mv->get_matrix(), true);
        its_merge(world_mesh, std::move(vol_its));
    }

    if (world_mesh.indices.empty())
        return false;

    its_merge_vertices(world_mesh);

    // ---- 2. Overhang face detection ----
    //
    // A face needs support when its normal points downward within
    // support_threshold_angle degrees of straight down.  The standard Orca
    // semantics: threshold_deg is the maximum overhang angle from a vertical
    // wall; faces flatter than that need support.  So a face is overhanging iff
    // (-n.z()) >= cos(threshold_rad), where n is the unit normal.
    const auto &verts = world_mesh.vertices;
    const auto &indices = world_mesh.indices;

    struct OverhangFace { int i0, i1, i2; };
    std::vector<OverhangFace> overhang_faces;
    overhang_faces.reserve(indices.size());

    for (const auto &tri : indices) {
        const Vec3f &v0 = verts[tri[0]];
        const Vec3f &v1 = verts[tri[1]];
        const Vec3f &v2 = verts[tri[2]];

        const Vec3f n = (v1 - v0).cross(v2 - v0);
        const double n_len = n.norm();
        if (n_len < EPSILON)
            continue; // degenerate

        const Vec3f n_u = n / n_len;

        // Standard Orca overhang test: face needs support if its downward
        // component (-n_z) exceeds cos of the threshold angle.
        if ((-n_u.z()) < cos_threshold)
            continue;

        // Minimum vertex Z: faces resting on/near the belt are already
        // supported.
        const double min_z = std::min({v0.z(), v1.z(), v2.z()});
        if (min_z <= 0.5)
            continue;

        // Triangle XY-projected area threshold: skip slivers.
        const double xy_area = 0.5 * std::abs(
            (v1.x() - v0.x()) * (v2.y() - v0.y()) -
            (v1.y() - v0.y()) * (v2.x() - v0.x()));
        if (xy_area <= 0.1)
            continue;

        overhang_faces.push_back({tri[0], tri[1], tri[2]});
    }

    if (overhang_faces.empty()) {
        BOOST_LOG_TRIVIAL(info)
            << "Belt gravity support: no overhang faces found";
        this->clear_support_layers();
        return true; // genuine: nothing needs support
    }

    // ---- 3. Prism construction (world frame) ----
    //
    // Each prism is a closed 8-triangle mesh:
    //   roof cap (1 tri), floor cap (1 tri), 3 side quads (2 tris each).
    // Roof vertices are the overhang triangle shifted DOWN by top_gap.
    // Floor vertices project to z=0.
    // Winding: outward normals.  For n.z() < 0 (original XY projection is CW),
    // the roof cap uses reversed vertex order for upward normal (+Z).
    // Floor cap uses original order for downward normal (-Z).
    // Side faces: standard extrusion triangulation for a CCW roof loop.
    indexed_triangle_set all_prisms;
    all_prisms.vertices.reserve(overhang_faces.size() * 6);
    all_prisms.indices.reserve(overhang_faces.size() * 8);

    for (const auto &of : overhang_faces) {
        const Vec3f v0 = verts[of.i0];
        const Vec3f v1 = verts[of.i1];
        const Vec3f v2 = verts[of.i2];

        // Roof: shift down by top_gap
        const Vec3f r0(v0.x(), v0.y(), v0.z() - top_gap);
        const Vec3f r1(v1.x(), v1.y(), v1.z() - top_gap);
        const Vec3f r2(v2.x(), v2.y(), v2.z() - top_gap);

        // Skip if roof would be at or below the floor
        if (r0.z() <= 0.f || r1.z() <= 0.f || r2.z() <= 0.f)
            continue;

        // Floor: project to z=0
        const Vec3f f0(v0.x(), v0.y(), 0.f);
        const Vec3f f1(v1.x(), v1.y(), 0.f);
        const Vec3f f2(v2.x(), v2.y(), 0.f);

        const int base = int(all_prisms.vertices.size());
        // Vertices stored in CCW roof order: original reversed (since n.z < 0)
        // r0, r2, r1 is CCW in XY when original v0,v1,v2 is CW.
        all_prisms.vertices.insert(all_prisms.vertices.end(), {r0, r2, r1,
                                                               f0, f2, f1});

        // Roof cap (CCW, normal +Z)
        all_prisms.indices.emplace_back(base + 0, base + 1, base + 2);
        // Floor cap (CW from above = reversed, normal -Z)
        all_prisms.indices.emplace_back(base + 5, base + 4, base + 3);
        // Side 0→1 (r0→r2): (r0, r2, f2), (r0, f2, f0)
        all_prisms.indices.emplace_back(base + 0, base + 1, base + 4);
        all_prisms.indices.emplace_back(base + 0, base + 4, base + 3);
        // Side 1→2 (r2→r1): (r2, r1, f1), (r2, f1, f2)
        all_prisms.indices.emplace_back(base + 1, base + 2, base + 5);
        all_prisms.indices.emplace_back(base + 1, base + 5, base + 4);
        // Side 2→0 (r1→r0): (r1, r0, f0), (r1, f0, f1)
        all_prisms.indices.emplace_back(base + 2, base + 0, base + 3);
        all_prisms.indices.emplace_back(base + 2, base + 3, base + 5);
    }

    if (all_prisms.indices.empty()) {
        BOOST_LOG_TRIVIAL(info)
            << "Belt gravity support: no prisms after clamping";
        this->clear_support_layers();
        return true;
    }

    // ---- 4. Transform into slicing space ----
    //
    // The model volumes get: Tslicer = T_delta ∘ T_world.
    // We already applied T_world to the prisms, so we only need T_delta on top.
    // Rebuild T_delta exactly as BeltSliceStrategy::apply_preslice_transforms
    // does: Tz(lift) ∘ R_belt ∘ P_preremap.
    // lift = max(0.0, -m_belt_min_z) per the same code path.
    Transform3d T_delta = Transform3d::Identity();
    bool has_delta = false;

    // P_preremap
    if (BeltTransformPipeline::has_preslice_remap(pcfg)) {
        T_delta = BeltTransformPipeline::build_preslice_remap(pcfg) * T_delta;
        has_delta = true;
    }

    // R_belt
    bool has_rotation = false;
    if (pcfg.belt_printer.value) {
        const Matrix3d rot = BeltTransformPipeline::build_rotation_matrix(pcfg, &has_rotation);
        if (has_rotation) {
            Transform3d belt_xform = Transform3d::Identity();
            belt_xform.linear() = rot;
            T_delta = belt_xform * T_delta;
            has_delta = true;
        }
    }

    // Tz(lift) — reuse the stored m_belt_min_z exactly as apply_preslice_transforms does
    if (has_delta && pcfg.belt_printer.value) {
        const double lift = (m_belt_min_z < 0.) ? -m_belt_min_z : 0.;
        if (lift > 0.) {
            Transform3d z_shift = Transform3d::Identity();
            z_shift.matrix()(2, 3) = lift;
            T_delta = z_shift * T_delta;
        }
    }

    if (has_delta)
        its_transform(all_prisms, T_delta, true);

    // ---- 5. Slice the prisms ----
    std::vector<float> zs;
    zs.reserve(m_layers.size());
    for (const Layer *l : m_layers)
        zs.emplace_back(static_cast<float>(l->slice_z));

    const auto throw_on_cancel = [this]() { this->throw_if_canceled(); };
    std::vector<ExPolygons> prism_slices = slice_mesh_ex(all_prisms, zs, throw_on_cancel);

    // ---- 6. Per-layer post-processing ----
    const double xy_gap = m_config.support_object_xy_distance.value;
    const coord_t xy_gap_scaled = scale_(xy_gap);

    BeltFloorContext belt_ctx;
    const bool has_belt_floor = belt_ctx.init(m_slicing_params, pcfg);

    const double min_area_scaled = sqr(scale_(1.0));

    for (size_t i = 0; i < m_layers.size(); ++i) {
        if (prism_slices[i].empty())
            continue;

        // Subtract model slices (with XY gap)
        ExPolygons model_offset = offset_ex(m_layers[i]->lslices, float(xy_gap_scaled));
        ExPolygons support_i = diff_ex(prism_slices[i], model_offset);

        // Belt-floor clip: keep only the region where support is allowed
        if (has_belt_floor && belt_ctx.is_active())
            support_i = intersection_ex(support_i,
                                        belt_ctx.valid_region_polygon(m_layers[i]->print_z));

        // Drop areas < 1 mm²
        ExPolygons filtered;
        filtered.reserve(support_i.size());
        for (auto &expoly : support_i)
            if (expoly.area() >= min_area_scaled)
                filtered.emplace_back(std::move(expoly));
        prism_slices[i] = std::move(filtered);
    }

    // ---- 7. Emit support layers + toolpaths ----
    this->clear_support_layers();

    // Count non-empty layers
    size_t support_layer_count = 0;
    for (const auto &s : prism_slices)
        if (!s.empty())
            ++support_layer_count;

    if (support_layer_count == 0) {
        BOOST_LOG_TRIVIAL(info)
            << "Belt gravity support: no support after post-processing";
        return true;
    }

    // Build SupportGeneratorLayer structures
    SupportGeneratorLayerStorage layer_storage;
    SupportGeneratorLayersPtr intermediate_layers;
    intermediate_layers.reserve(m_layers.size());

    for (size_t i = 0; i < m_layers.size(); ++i) {
        if (prism_slices[i].empty())
            continue;

        SupportGeneratorLayer &sgl = layer_storage.allocate(SupporLayerType::Intermediate);
        sgl.print_z = m_layers[i]->print_z;
        sgl.bottom_z = m_layers[i]->print_z - m_layers[i]->height;
        sgl.height = m_layers[i]->height;
        sgl.polygons = to_polygons(prism_slices[i]);
        intermediate_layers.push_back(&sgl);

        // Create corresponding support layer
        this->add_support_layer(int(i), 0, m_layers[i]->height, m_layers[i]->print_z);
    }

    // Empty raft/contact/interface layers (v1 simplification: all areas are base).
    SupportGeneratorLayersPtr raft_layers;
    SupportGeneratorLayersPtr bottom_contacts;
    SupportGeneratorLayersPtr top_contacts;
    SupportGeneratorLayersPtr interface_layers;
    SupportGeneratorLayersPtr base_interface_layers;

    // Build support parameters the same way PrintObjectSupportMaterial does
    const SupportParameters support_params(*this);

    // Generate toolpaths through the native pipeline
    generate_support_toolpaths(
        this->support_layers(),
        m_config,
        support_params,
        m_slicing_params,
        raft_layers,
        bottom_contacts,
        top_contacts,
        intermediate_layers,
        interface_layers,
        base_interface_layers);

    // ---- 8. Logging ----
    BOOST_LOG_TRIVIAL(info)
        << "Belt gravity support: "
        << overhang_faces.size() << " overhang faces, "
        << (all_prisms.indices.size() / 8) << " prisms, "
        << support_layer_count << " support layers";

    return true;
}

} // namespace Slic3r
