#ifndef slic3r_SketchImport_hpp_
#define slic3r_SketchImport_hpp_

#include "Point.hpp"   // Vec2d

#include <string>
#include <vector>

namespace Slic3r {

// A rigid imported region: contour[0] = outer loop, contour[1..] = holes;
// points in plane (u,v) millimetres. The nested vector type matches
// CadFeature::imported_regions exactly, so results assign directly.
using ImportRegion  = std::vector<std::vector<Vec2d>>;
using ImportRegions = std::vector<ImportRegion>;

// Vectorize UTF-8 text into filled regions (mm), centred on the origin.
// `size_mm` is the cap/line height. `font_path` empty -> a bundled default
// font (resources/fonts). Returns an empty vector on any failure.
ImportRegions text_to_regions(const std::string& utf8, double size_mm,
                              const std::string& font_path = std::string());

// Parse an SVG file's filled paths into regions (mm), centred on the origin.
// `scale` multiplies the authored size (1.0 = as authored). Returns an empty
// vector on any failure.
ImportRegions svg_to_regions(const std::string& svg_path, double scale = 1.0);

} // namespace Slic3r

#endif // slic3r_SketchImport_hpp_
