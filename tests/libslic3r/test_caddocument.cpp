#include <catch2/catch.hpp>

#include "libslic3r/CadDocument.hpp"
#include "libslic3r/SketchEngine.hpp"

using namespace Slic3r;

TEST_CASE("CadDocument profile sketch -> extrude -> solid", "[CadDocument]")
{
    CadDocument doc;

    SketchProfile sp;
    sp.points.push_back(Vec2d(-10, -10));
    sp.points.push_back(Vec2d( 10, -10));
    sp.points.push_back(Vec2d( 10,  10));
    sp.points.push_back(Vec2d(-10,  10));
    sp.closed = true;

    int sk_idx = doc.add_sketch_profile(sp, SketchPlane::XY(), "SquareProfile");
    REQUIRE(sk_idx >= 0);
    doc.add_extrude(sk_idx, 5.0, false, BooleanMode::New, "Extrude1");

    bool ok = doc.recompute();
    REQUIRE(ok);
    REQUIRE(doc.error.empty());
    REQUIRE(doc.display_mesh.facets_count() > 0);
}

TEST_CASE("CadDocument legacy Rectangle sketch still works", "[CadDocument]")
{
    CadDocument doc;

    int sk_idx = doc.add_sketch(SketchShape::Rectangle, SketchPlane::XY(),
                                20, 20, 10, "RectSketch");
    REQUIRE(sk_idx >= 0);
    doc.add_extrude(sk_idx, 5.0, false, BooleanMode::New, "Extrude1");

    bool ok = doc.recompute();
    REQUIRE(ok);
    REQUIRE(doc.error.empty());
    REQUIRE(doc.display_mesh.facets_count() > 0);
}

TEST_CASE("CadDocument preview with profile on self-contained candidate", "[CadDocument]")
{
    CadDocument doc;

    CadFeature candidate;
    candidate.type     = CadFeatureType::Extrude;
    candidate.plane    = SketchPlane::XY();
    candidate.distance = 4;
    candidate.mode     = BooleanMode::New;
    // sketch_ref is -1 by default -> apply_feature uses candidate's own params

    SketchProfile tri;
    tri.points.push_back(Vec2d(0,  0));
    tri.points.push_back(Vec2d(10, 0));
    tri.points.push_back(Vec2d(5,  8.66));
    tri.closed   = true;
    candidate.profile = tri;

    TriangleMesh mesh;
    std::string  err;
    bool ok = doc.preview(candidate, mesh, err);
    REQUIRE(ok);
    REQUIRE(mesh.facets_count() > 0);
}
