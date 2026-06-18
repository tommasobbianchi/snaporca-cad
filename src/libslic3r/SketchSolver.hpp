#ifndef slic3r_SketchSolver_hpp_
#define slic3r_SketchSolver_hpp_

// Bridge from SnapOrca's SketchEntity / SketchEntityConstraintDef model onto the
// vendored SolveSpace constraint solver (src/libslic3r/slvs, libslvs). Replaces the
// hand-rolled SketchConstraints: full constraint set, real DoF counting, and
// over-constrained (bad-constraint) detection. Solves on a fixed 2D XY workplane.

#include "SketchEngine.hpp"
#include <vector>

namespace Slic3r {

struct SketchSolveResult {
    bool             ok{false};   // solver converged & consistent
    int              dof{-1};     // remaining degrees of freedom (>0 under-constrained)
    int              result{0};   // raw SLVS_RESULT_* code
    std::vector<int> bad;         // indices (into `constraints`) of conflicting constraints
};

// Solve `constraints` over `entities` in place (writes solved coordinates back into the
// entities; arc angles are reflowed preserving sweep direction). No-op success when
// `constraints` is empty.
SketchSolveResult sketch_solve(std::vector<SketchEntity>& entities,
                               const std::vector<SketchEntityConstraintDef>& constraints);

} // namespace Slic3r

#endif
