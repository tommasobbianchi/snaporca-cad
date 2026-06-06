#ifndef slic3r_MachineFrameTransform_hpp_
#define slic3r_MachineFrameTransform_hpp_

#include "../libslic3r.h"
#include "../Point.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {

// Post-stage machine-frame transform for belt printers.
//
// Applied in BeltGCodeWriter::to_machine_coords AFTER the back-transform,
// the gcode_remap_* axis remap and per-axis origin snap.  Maps Cartesian
// (axis-permuted) G-code coordinates into the printer's physical machine
// frame using:
//   gcode_shear_x/y/z + _angle + _from
//   gcode_scale_x/y/z + _angle
//
// Composition follows belt_gcode_transform_order (shear * scale or scale * shear).
class MachineFrameTransform
{
public:
    MachineFrameTransform() = default;

    // Initialize from belt printer config.  Returns true if a non-identity
    // transform was computed.  Inactive when belt_printer is disabled or
    // both shear and scale are identity.
    bool init_from_config(const PrintConfig &config);

    // Apply the transform to a point.  Returns pos unchanged if not active.
    Vec3d apply(const Vec3d &pos) const;

    bool is_active() const { return m_active; }

    // The composed shear*scale transform (identity when inactive). Exposed so the
    // G-code viewer can build the machine->model back-transform for the upright
    // ("designed") belt preview.
    const Transform3d& transform() const { return m_transform; }

private:
    bool        m_active    = false;
    Transform3d m_transform = Transform3d::Identity();
};

} // namespace Slic3r

#endif // slic3r_MachineFrameTransform_hpp_
