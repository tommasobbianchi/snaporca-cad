#include <catch2/catch.hpp>

#include "libslic3r/CadDocument.hpp"
#include "libslic3r/SketchEngine.hpp"
#include "libslic3r/SketchImport.hpp"
#include "libslic3r/Utils.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>

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

TEST_CASE("CadDocument solve_sketch_feature snaps a rough quad to a rectangle", "[CadDocument]")
{
    CadDocument doc;
    SketchProfile sp;
    sp.points = { Vec2d(0,0), Vec2d(8,1), Vec2d(9,5), Vec2d(-1,4) };
    sp.closed = true;
    int sk = doc.add_sketch_profile(sp, SketchPlane::XY(), "S");

    auto& cons = doc.features[sk].constraints;
    cons.push_back({SketchConstraintType::Fix,        0,-1,-1,-1, 0});
    cons.push_back({SketchConstraintType::LockX,      0,-1,-1,-1, 0});
    cons.push_back({SketchConstraintType::LockY,      0,-1,-1,-1, 0});
    cons.push_back({SketchConstraintType::Horizontal, 0, 1,-1,-1, 0});
    cons.push_back({SketchConstraintType::Vertical,   1, 2,-1,-1, 0});
    cons.push_back({SketchConstraintType::Horizontal, 2, 3,-1,-1, 0});
    cons.push_back({SketchConstraintType::Vertical,   3, 0,-1,-1, 0});
    cons.push_back({SketchConstraintType::Distance,   0, 1,-1,-1, 10});
    cons.push_back({SketchConstraintType::Distance,   1, 2,-1,-1, 6});

    REQUIRE(doc.solve_sketch_feature(sk));
    const auto& pts = doc.features[sk].profile.points;
    REQUIRE_THAT(pts[1].x(), Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(pts[1].y(), Catch::Matchers::WithinAbs(0.0,  1e-3));
    REQUIRE_THAT(pts[2].x(), Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(pts[2].y(), Catch::Matchers::WithinAbs(6.0,  1e-3));
    REQUIRE_THAT(pts[3].x(), Catch::Matchers::WithinAbs(0.0,  1e-3));
    REQUIRE_THAT(pts[3].y(), Catch::Matchers::WithinAbs(6.0,  1e-3));

    // the solved profile still extrudes into a solid
    doc.add_extrude(sk, 5.0, false, BooleanMode::New, "E");
    REQUIRE(doc.recompute());
    REQUIRE(doc.display_mesh.facets_count() > 0);
}

TEST_CASE("sketch entities -> wire -> extrude", "[CadDocument]")
{
    SECTION("square from 4 lines") {
        CadDocument doc;

        CadFeature sk;
        sk.type    = CadFeatureType::Sketch;
        sk.name    = "square";
        sk.plane   = SketchPlane::XY();
        sk.entities = {
            {SketchEntity::Type::Line, Vec2d(-10,-10), Vec2d(10,-10)},
            {SketchEntity::Type::Line, Vec2d(10,-10), Vec2d(10,10)},
            {SketchEntity::Type::Line, Vec2d(10,10), Vec2d(-10,10)},
            {SketchEntity::Type::Line, Vec2d(-10,10), Vec2d(-10,-10)},
        };
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.name       = "extrude";
        ex.sketch_ref = 0;
        ex.distance   = 5;
        ex.mode       = BooleanMode::New;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);

        auto bb = doc.display_mesh.bounding_box();
        auto sz = bb.max - bb.min;
        REQUIRE(std::abs(sz.x() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  5.0) < 0.5);
    }

    SECTION("single circle") {
        CadDocument doc;

        CadFeature sk;
        sk.type    = CadFeatureType::Sketch;
        sk.name    = "circle";
        sk.plane   = SketchPlane::XY();
        sk.entities = {
            {SketchEntity::Type::Circle, Vec2d(0,0), Vec2d(0,0), Vec2d(0,0), 10.0},
        };
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.name       = "extrude";
        ex.sketch_ref = 0;
        ex.distance   = 8;
        ex.mode       = BooleanMode::New;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);

        auto bb = doc.display_mesh.bounding_box();
        auto sz = bb.max - bb.min;
        REQUIRE(std::abs(sz.x() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  8.0) < 0.5);
    }

    SECTION("construction line excluded") {
        CadDocument doc;

        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.name  = "square_with_construction";
        sk.plane = SketchPlane::XY();

        SketchEntity cline;
        cline.type         = SketchEntity::Type::Line;
        cline.p0           = Vec2d(-10, -10);
        cline.p1           = Vec2d(10, 10);
        cline.construction = true;

        sk.entities = {
            {SketchEntity::Type::Line, Vec2d(-10,-10), Vec2d(10,-10)},
            {SketchEntity::Type::Line, Vec2d(10,-10), Vec2d(10,10)},
            {SketchEntity::Type::Line, Vec2d(10,10), Vec2d(-10,10)},
            {SketchEntity::Type::Line, Vec2d(-10,10), Vec2d(-10,-10)},
            cline,
        };
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.name       = "extrude";
        ex.sketch_ref = 0;
        ex.distance   = 5;
        ex.mode       = BooleanMode::New;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);

        auto bb = doc.display_mesh.bounding_box();
        auto sz = bb.max - bb.min;
        REQUIRE(std::abs(sz.x() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  5.0) < 0.5);
    }
}

// Mirrors the GUI interactive-sketch commit path (DesignPanel ->
// add_sketch_entities) for the Fase 4.1 entity drawing tools: a corner-rect and
// a center-rect produce 4 closed Line entities; a center-circle produces 1
// Circle entity. add_sketch_entities must store them and extrude into a solid.
TEST_CASE("add_sketch_entities commit path -> extrude", "[CadDocument]")
{
    SECTION("corner-rect 4 lines -> 30x16x5") {
        CadDocument doc;
        // Corner A=(-15,-8), B=(15,8): the tool's push_closed_lines order.
        const Vec2d A(-15, -8), B(15, 8);
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, A,                 Vec2d(B.x(), A.y())},
            {SketchEntity::Type::Line, Vec2d(B.x(),A.y()), B},
            {SketchEntity::Type::Line, B,                 Vec2d(A.x(), B.y())},
            {SketchEntity::Type::Line, Vec2d(A.x(),B.y()), A},
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "Sketch1");
        REQUIRE(sk == 0);
        doc.add_extrude(sk, 5.0, false, BooleanMode::New, "Extrude1");

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);
        auto sz = doc.display_mesh.bounding_box().size();
        REQUIRE(std::abs(sz.x() - 30.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 16.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  5.0) < 0.5);
    }

    SECTION("center-circle 1 entity -> r=7 cylinder") {
        CadDocument doc;
        SketchEntity c;
        c.type = SketchEntity::Type::Circle;
        c.center = Vec2d(0, 0);
        c.p0 = Vec2d(0, 0);
        c.radius = 7.0;
        int sk = doc.add_sketch_entities({c}, SketchPlane::XY(), "Sketch1");
        doc.add_extrude(sk, 4.0, false, BooleanMode::New, "Extrude1");

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);
        auto sz = doc.display_mesh.bounding_box().size();
        REQUIRE(std::abs(sz.x() - 14.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 14.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  4.0) < 0.5);
    }
}

// A slot (stadium): 2 lines + 2 semicircular Arc entities forming one closed
// loop — the shape the Fase 4.1b Slot tool emits. Validates the kernel's Arc
// edge path (GC_MakeArcOfCircle via center/radius/start_angle/end_angle, with
// the mid reconstructed at (start+end)/2) inside a mixed Line/Arc wire.
TEST_CASE("slot (line+arc closed wire) -> extrude", "[CadDocument]")
{
    const double PI = 3.14159265358979323846;
    CadDocument doc;

    // Centerline ends c0=(-10,0), c1=(10,0); half-width w=5 → stadium 30 x 10.
    SketchEntity top;   // top line A0(-10,5) -> A1(10,5)
    top.type = SketchEntity::Type::Line; top.p0 = Vec2d(-10, 5); top.p1 = Vec2d(10, 5);

    SketchEntity cap1;  // right cap @c1=(10,0): A1(10,5) -> B1(10,-5) through (15,0)
    cap1.type = SketchEntity::Type::Arc; cap1.center = Vec2d(10, 0); cap1.radius = 5;
    cap1.p0 = Vec2d(10, 5); cap1.p1 = Vec2d(10, -5);
    cap1.start_angle = PI / 2; cap1.end_angle = -PI / 2;   // mid angle 0 -> (15,0)

    SketchEntity bot;   // bottom line B1(10,-5) -> B0(-10,-5)
    bot.type = SketchEntity::Type::Line; bot.p0 = Vec2d(10, -5); bot.p1 = Vec2d(-10, -5);

    SketchEntity cap0;  // left cap @c0=(-10,0): B0(-10,-5) -> A0(-10,5) through (-15,0)
    cap0.type = SketchEntity::Type::Arc; cap0.center = Vec2d(-10, 0); cap0.radius = 5;
    cap0.p0 = Vec2d(-10, -5); cap0.p1 = Vec2d(-10, 5);
    cap0.start_angle = -PI / 2; cap0.end_angle = -3 * PI / 2; // mid angle -PI -> (-15,0)

    int sk = doc.add_sketch_entities({top, cap1, bot, cap0}, SketchPlane::XY(), "Slot");
    doc.add_extrude(sk, 4.0, false, BooleanMode::New, "Extrude1");

    REQUIRE(doc.recompute());
    REQUIRE(doc.error.empty());
    REQUIRE(doc.display_mesh.facets_count() > 0);
    auto sz = doc.display_mesh.bounding_box().size();
    REQUIRE(std::abs(sz.x() - 30.0) < 0.5);
    REQUIRE(std::abs(sz.y() - 10.0) < 0.5);
    REQUIRE(std::abs(sz.z() -  4.0) < 0.5);
}

// Onshape-style constraints on coexisting entities (Fase 4.2). The solver maps
// each Line/Point entity endpoint to a solver variable, applies the entity
// constraints, and writes the solved coordinates back into the entities.
TEST_CASE("entity constraints: solve on SketchEntity endpoints", "[CadDocument]")
{
    using R = SketchPointRole;
    using T = SketchConstraintType;

    auto dir = [](const SketchEntity& e) { return Vec2d(e.p1 - e.p0); };

    SECTION("perpendicular rotates line1 normal to a pinned line0") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(10,0)},   // line0 (pinned)
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(7,7)},    // line1 @45 deg
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,           0, -1, R::P0, R::P0, 0.0});
        ec.push_back({T::Fix,           0, -1, R::P1, R::P0, 0.0});
        ec.push_back({T::Perpendicular, 0,  1, R::P0, R::P0, 0.0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        // line0 stayed put.
        REQUIRE(std::abs(e[0].p0.x() - 0.0)  < 1e-6);
        REQUIRE(std::abs(e[0].p1.x() - 10.0) < 1e-6);
        // line1 is now perpendicular to line0: directions dot to ~0.
        const double d = dir(e[0]).dot(dir(e[1]));
        REQUIRE(std::abs(d) < 1e-6);
    }

    // Driving length: the Dimension tool records a Distance between a line's own
    // P0/P1 (committed via add_sketch_entities' constraints arg). Solving drives
    // the line to that exact length.
    SECTION("driving length: Distance(P0,P1) sets a line's length") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(10,0)},   // length 10
        };
        std::vector<SketchEntityConstraintDef> cons;
        cons.push_back({T::Fix,      0, -1, R::P0, R::P0, 0.0});   // pin the start
        cons.push_back({T::Distance, 0,  0, R::P0, R::P1, 25.0});  // length -> 25
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S", cons);
        REQUIRE(doc.features[sk].entity_constraints.size() == 2);  // constraints stored
        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs((e[0].p1 - e[0].p0).norm() - 25.0) < 1e-6);
    }

    SECTION("parallel flattens line1 onto a pinned horizontal line0") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(10,0)},   // line0 (pinned)
            {SketchEntity::Type::Line, Vec2d(0,5), Vec2d(7,9)},    // line1 tilted
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,      0, -1, R::P0, R::P0, 0.0});
        ec.push_back({T::Fix,      0, -1, R::P1, R::P0, 0.0});
        ec.push_back({T::Parallel, 0,  1, R::P0, R::P0, 0.0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        const Vec2d d0 = dir(e[0]), d1 = dir(e[1]);
        const double cross = d0.x() * d1.y() - d0.y() * d1.x();
        REQUIRE(std::abs(cross) < 1e-6);
    }

    SECTION("coincident merges a line endpoint onto another") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0),  Vec2d(10,0)},   // line0
            {SketchEntity::Type::Line, Vec2d(12,1), Vec2d(20,1)},   // line1
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Coincident, 0, 1, R::P1, R::P0, 0.0});   // line0.end == line1.start

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        const Vec2d gap = Vec2d(e[0].p1 - e[1].p0);
        REQUIRE(gap.norm() < 1e-6);
    }

    SECTION("horizontal levels a tilted line's endpoints") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(10,2)},   // tilted line
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Horizontal, 0, 0, R::P0, R::P1, 0.0});   // p0.y == p1.y

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs(e[0].p0.y() - e[0].p1.y()) < 1e-6);
    }
}

TEST_CASE("entity constraints: arc/circle registration + concentric", "[CadDocument]")
{
    using R = SketchPointRole;
    using T = SketchConstraintType;

    SECTION("concentric centers coincide") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Circle, Vec2d(0,0), Vec2d(0,0), Vec2d(0,0), 5.0},   // circle0
            {SketchEntity::Type::Circle, Vec2d(10,2), Vec2d(10,2), Vec2d(10,2), 3.0}, // circle1
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,        0, -1, R::Center, R::Center, 0.0});
        ec.push_back({T::Concentric, 0,  1, R::Center, R::Center, 0.0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE((e[1].center - e[0].center).norm() < 1e-6);
        REQUIRE(e[0].center.x() < 1e-6);
        REQUIRE(e[0].center.y() < 1e-6);
    }

    SECTION("arc reflow keeps radius and angle consistent") {
        CadDocument doc;
        const double PI2 = M_PI / 2;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Arc, Vec2d(5,0), Vec2d(0,5), Vec2d(0,0), 5.0, 0.0, PI2},
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix, 0, -1, R::Center, R::Center, 0.0});
        ec.push_back({T::Fix, 0, -1, R::P0,     R::P0,     0.0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs((e[0].p0 - e[0].center).norm() - 5.0) < 1e-6);
        REQUIRE(std::abs(e[0].start_angle - 0.0) < 1e-6);
        REQUIRE(std::abs(e[0].end_angle - PI2) < 1e-3);
        REQUIRE(e[0].end_angle > e[0].start_angle);
    }
}

TEST_CASE("entity constraints: radius/diameter dimensions", "[CadDocument]")
{
    using R = SketchPointRole;
    using T = SketchConstraintType;

    SECTION("circle radius dimension") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Circle, Vec2d(0,0), Vec2d(0,0), Vec2d(0,0), 5.0},
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,       0, -1, R::Center, R::Center, 0.0, -1, R::P0});
        ec.push_back({T::Radius,    0, -1, R::Center, R::P0,     8.0, -1, R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs(e[0].radius - 8.0) < 1e-9);
    }

    SECTION("circle diameter dimension") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Circle, Vec2d(0,0), Vec2d(0,0), Vec2d(0,0), 5.0},
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,       0, -1, R::Center, R::Center, 0.0, -1, R::P0});
        ec.push_back({T::Diameter,  0, -1, R::Center, R::P0,     20.0, -1, R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs(e[0].radius - 10.0) < 1e-9);
    }

    SECTION("arc radius rescales endpoints") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Arc, Vec2d(5,0), Vec2d(0,5), Vec2d(0,0), 5.0, 0.0, M_PI/2},
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,       0, -1, R::Center, R::Center, 0.0, -1, R::P0});
        ec.push_back({T::Radius,    0, -1, R::Center, R::P0,     10.0, -1, R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs(e[0].radius - 10.0) < 1e-9);
        REQUIRE((e[0].p0 - Vec2d(10,0)).norm() < 1e-6);
        REQUIRE((e[0].p1 - Vec2d(0,10)).norm() < 1e-6);
    }
}

// PointOnLine (Fase: persistent positioning). A point-like entity is held on a
// line (value 0) or at perpendicular distance `value`. Drives e.g. a circle centre
// onto a construction axis and, being a real constraint, keeps it there on re-solve.
TEST_CASE("entity constraints: point-on-line positions a centre onto an axis", "[CadDocument]")
{
    using R = SketchPointRole;
    using T = SketchConstraintType;

    SECTION("circle centre snaps onto a pinned axis (value 0) and persists") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line,   Vec2d(0,0), Vec2d(10,0)},                 // axis (X)
            {SketchEntity::Type::Circle, Vec2d(5,7), Vec2d(5,7), Vec2d(5,7), 3.0}, // off-axis centre
        };
        std::vector<SketchEntityConstraintDef> cons;
        cons.push_back({T::Fix,         0, -1, R::P0,     R::P0, 0.0});   // pin axis endpoints
        cons.push_back({T::Fix,         0, -1, R::P1,     R::P1, 0.0});
        cons.push_back({T::PointOnLine, 1,  0, R::Center, R::P0, 0.0});   // centre onto axis
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S", cons);
        REQUIRE(doc.solve_sketch_feature(sk));
        REQUIRE(std::abs(doc.features[sk].entities[1].center.y()) < 1e-6);   // on the axis
        // Re-solving keeps it on the axis (a driving constraint, not a one-shot move).
        REQUIRE(doc.solve_sketch_feature(sk));
        REQUIRE(std::abs(doc.features[sk].entities[1].center.y()) < 1e-6);
    }

    SECTION("non-zero perpendicular distance via the free solve_sketch_entities") {
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line,  Vec2d(0,0), Vec2d(10,0)},
            {SketchEntity::Type::Point, Vec2d(4,9)},                       // point above the axis
        };
        std::vector<SketchEntityConstraintDef> cons;
        cons.push_back({T::Fix,         0, -1, R::P0, R::P0, 0.0});
        cons.push_back({T::Fix,         0, -1, R::P1, R::P1, 0.0});
        cons.push_back({T::PointOnLine, 1,  0, R::P0, R::P0, 2.0});        // hold at distance 2
        REQUIRE(solve_sketch_entities(ents, cons));
        REQUIRE(std::abs(std::abs(ents[1].p0.y()) - 2.0) < 1e-6);         // 2 mm off the axis
    }
}

TEST_CASE("entity constraints: tangent/midpoint/symmetric/angle", "[CadDocument]")
{
    using R = SketchPointRole;
    using T = SketchConstraintType;

    auto dir = [](const SketchEntity& e) { return Vec2d(e.p1 - e.p0); };

    SECTION("angle 90 between two lines") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(10,0)},    // line0
            {SketchEntity::Type::Line, Vec2d(0,0), Vec2d(5,5)},     // line1
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,  0,-1, R::P0,R::P0, 0.0,  -1,R::P0});
        ec.push_back({T::Fix,  0,-1, R::P1,R::P0, 0.0,  -1,R::P0});
        ec.push_back({T::Fix,  1,-1, R::P0,R::P0, 0.0,  -1,R::P0});
        ec.push_back({T::Angle,0, 1, R::P0,R::P0, M_PI/2,-1,R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        const Vec2d d0 = dir(e[0]).normalized();
        const Vec2d d1 = dir(e[1]).normalized();
        REQUIRE(std::abs(d0.dot(d1)) < 1e-3);
    }

    SECTION("midpoint of a line") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Line,  Vec2d(0,0), Vec2d(10,0)},  // line0
            {SketchEntity::Type::Point, Vec2d(3,9)},                // point p
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,     0,-1, R::P0,R::P0, 0.0,-1,R::P0});
        ec.push_back({T::Fix,     0,-1, R::P1,R::P0, 0.0,-1,R::P0});
        ec.push_back({T::Midpoint,1, 0, R::P0,R::P0, 0.0,-1,R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE((e[1].p0 - Vec2d(5,0)).norm() < 1e-3);
    }

    SECTION("tangent line to circle") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Circle, Vec2d(0,0), Vec2d(0,0), Vec2d(0,0), 5.0},   // circle0 r=5
            {SketchEntity::Type::Line,   Vec2d(-10,8), Vec2d(10,8)},                  // line1 y=8
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,        0,-1, R::Center,R::Center,  0.0,-1,R::P0});
        ec.push_back({T::LockX,      1,-1, R::P0,    R::P0,   -10.0,-1,R::P0});
        ec.push_back({T::LockX,      1,-1, R::P1,    R::P0,    10.0,-1,R::P0});
        ec.push_back({T::Horizontal, 1, 1, R::P0,    R::P1,     0.0,-1,R::P0});
        ec.push_back({T::Tangent,    0, 1, R::Center,R::P0,     0.0,-1,R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE(std::abs(std::abs(e[1].p0.y()) - 5.0) < 1e-3);
    }

    SECTION("symmetric across a line") {
        CadDocument doc;
        std::vector<SketchEntity> ents = {
            {SketchEntity::Type::Point, Vec2d(2,3)},            // pointA
            {SketchEntity::Type::Point, Vec2d(-1,1)},           // pointB
            {SketchEntity::Type::Line,  Vec2d(0,0), Vec2d(0,10)}// axis (Y axis)
        };
        int sk = doc.add_sketch_entities(ents, SketchPlane::XY(), "S");
        auto& ec = doc.features[sk].entity_constraints;
        ec.push_back({T::Fix,       0,-1, R::P0,R::P0, 0.0,-1,R::P0});
        ec.push_back({T::Fix,       2,-1, R::P0,R::P0, 0.0,-1,R::P0});
        ec.push_back({T::Fix,       2,-1, R::P1,R::P0, 0.0,-1,R::P0});
        ec.push_back({T::Symmetric, 0, 1, R::P0,R::P0, 0.0, 2,R::P0});

        REQUIRE(doc.solve_sketch_feature(sk));
        const auto& e = doc.features[sk].entities;
        REQUIRE((e[1].p0 - Vec2d(-2,3)).norm() < 1e-3);
    }
}

TEST_CASE("imported text glyphs all extrude without failing (charset sweep)", "[CadDocument]")
{
    std::string font = resources_dir().empty()
        ? std::string("resources/fonts/HarmonyOS_Sans_SC_Regular.ttf")
        : resources_dir() + "/fonts/HarmonyOS_Sans_SC_Regular.ttf";
    {
        std::ifstream probe(font);
        if (!probe.good()) {
            SUCCEED("bundled font not reachable in this environment; covered live on :10");
            return;
        }
    }

    const std::string charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "@#$%&*()[]{}<>?/+-=.,;:!"
        "\xC3\xA0\xC3\xA8\xC3\xA9\xC3\xAC\xC3\xB2\xC3\xB9";   // à è é ì ò ù (UTF-8)

    std::string failures;
    for (char c : charset) {
        ImportRegions regs = text_to_regions(std::string(1, c), 12.0, font);
        if (regs.empty()) continue;
        CadDocument doc;
        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = regs;
        doc.features.push_back(sk);
        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 3;
        doc.features.push_back(ex);
        if (!doc.recompute() || !doc.error.empty())
            failures += c;
    }
    INFO("glyphs that failed to extrude: [" << failures << "]");
    CHECK(failures.empty());

    // A realistic multi-glyph word must extrude too.
    {
        ImportRegions regs = text_to_regions("Snapmaker", 12.0, font);
        REQUIRE_FALSE(regs.empty());
        CadDocument doc;
        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = regs;
        doc.features.push_back(sk);
        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 3;
        doc.features.push_back(ex);
        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);
    }
}

TEST_CASE("imported regions: faces-with-holes extrude (Text/SVG carrier)", "[CadDocument]")
{
    SECTION("square with a square hole -> tube volume") {
        CadDocument doc;

        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.name  = "art";
        sk.plane = SketchPlane::XY();
        // One region: outer 20x20 (CCW) + inner 8x8 hole (CW).
        sk.imported_regions = {{
            {Vec2d(-10,-10), Vec2d(10,-10), Vec2d(10,10), Vec2d(-10,10)},   // outer
            {Vec2d(-4,-4),   Vec2d(-4,4),   Vec2d(4,4),   Vec2d(4,-4)},     // hole (reversed winding)
        }};
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.name       = "extrude";
        ex.sketch_ref = 0;
        ex.distance   = 5;
        ex.mode       = BooleanMode::New;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE(doc.display_mesh.facets_count() > 0);

        // (20*20 - 8*8) * 5 = 1680 mm^3
        REQUIRE_THAT(double(doc.display_mesh.volume()), Catch::Matchers::WithinRel(1680.0, 0.02));

        auto sz = doc.display_mesh.bounding_box().size();
        REQUIRE(std::abs(sz.x() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.y() - 20.0) < 0.5);
        REQUIRE(std::abs(sz.z() -  5.0) < 0.5);
    }

    SECTION("inverted winding (CW outer, CCW hole) keeps body solid, counter empty") {
        // Real glyph contours (P, e, o, 8) arrive with outer CW + hole CCW. The
        // extrude must NORMALISE winding so the letter BODY is solid and the
        // counter is the hole — not the inverse (the reported bug). 20x20 outer
        // CW with an 8x8 hole CCW -> volume (400-64)*5 = 1680, NOT the inverse.
        CadDocument doc;
        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = {{
            {Vec2d(-10,-10), Vec2d(-10,10), Vec2d(10,10), Vec2d(10,-10)},  // outer CW
            {Vec2d(-4,-4),   Vec2d(4,-4),   Vec2d(4,4),   Vec2d(-4,4)},    // hole CCW
        }};
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 5;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE_THAT(double(doc.display_mesh.volume()), Catch::Matchers::WithinRel(1680.0, 0.02));
    }

    SECTION("degenerate / duplicate points are sanitised, not fatal") {
        // FreeType/SVG flattening can emit repeated points; the extrude must
        // survive them (previously to_occt_wire threw and failed the whole op).
        CadDocument doc;
        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = {{
            // 20x20 outer square with consecutive dups + an explicit closing dup
            {Vec2d(-10,-10), Vec2d(-10,-10), Vec2d(10,-10), Vec2d(10,-10),
             Vec2d(10,10),   Vec2d(-10,10),  Vec2d(-10,-10)},
        }};
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 5;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE_THAT(double(doc.display_mesh.volume()), Catch::Matchers::WithinRel(2000.0, 0.02));
    }

    SECTION("a degenerate region is skipped, valid ones still extrude") {
        CadDocument doc;
        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = {
            { {Vec2d(0,0), Vec2d(0,0), Vec2d(0,0)} },              // collapses to nothing
            { {Vec2d(0,0), Vec2d(5,0), Vec2d(5,5), Vec2d(0,5)} },  // valid 5x5
        };
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 4;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        REQUIRE_THAT(double(doc.display_mesh.volume()), Catch::Matchers::WithinRel(100.0, 0.02));
    }

    SECTION("two disjoint regions form one shape") {
        CadDocument doc;

        CadFeature sk;
        sk.type  = CadFeatureType::Sketch;
        sk.plane = SketchPlane::XY();
        sk.imported_regions = {
            {{Vec2d(0,0),  Vec2d(5,0),  Vec2d(5,5),  Vec2d(0,5)}},
            {{Vec2d(10,0), Vec2d(15,0), Vec2d(15,5), Vec2d(10,5)}},
        };
        doc.features.push_back(sk);

        CadFeature ex;
        ex.type       = CadFeatureType::Extrude;
        ex.sketch_ref = 0;
        ex.distance   = 3;
        doc.features.push_back(ex);

        REQUIRE(doc.recompute());
        REQUIRE(doc.error.empty());
        // 2 * (5*5*3) = 150 mm^3
        REQUIRE_THAT(double(doc.display_mesh.volume()), Catch::Matchers::WithinRel(150.0, 0.02));
    }
}

TEST_CASE("tessellate tracks per-triangle face id", "[CadDocument]")
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10., 10., 10.).Shape();
    std::vector<int> tf;
    TriangleMesh m = SketchEngine::tessellate(box, tf);

    REQUIRE(tf.size() == m.its.indices.size());
    REQUIRE(!tf.empty());

    std::set<int> distinct(tf.begin(), tf.end());
    REQUIRE(distinct.size() == 6);
    REQUIRE(*distinct.begin() == 0);
    REQUIRE(*distinct.rbegin() == 5);

    for (int fid : distinct) {
        int count = 0;
        for (int x : tf) if (x == fid) ++count;
        REQUIRE(count >= 2);
    }

    REQUIRE(m.its.indices.size() >= 12);
}

TEST_CASE("extrude two-sided + through-all + intersect", "[CadDocument]")
{
    using namespace Slic3r;
    SketchPlane xy = SketchPlane::XY();
    // a 10x10 square wire centred on origin
    SketchProfile sp;
    sp.points = { Vec2d(-5,-5), Vec2d(5,-5), Vec2d(5,5), Vec2d(-5,5) };
    sp.closed = true;
    TopoDS_Wire w = sp.to_occt_wire(xy);

    SECTION("two-sided height = up+down") {
        TopoDS_Shape s = SketchEngine::make_extrude_two_sided(w, xy, 10.0, 4.0);
        REQUIRE_FALSE(s.IsNull());
        Bnd_Box bb; BRepBndLib::Add(s, bb);
        double xmin,ymin,zmin,xmax,ymax,zmax; bb.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        REQUIRE_THAT(zmax - zmin, Catch::Matchers::WithinAbs(14.0, 0.05));   // 10 up + 4 down
        REQUIRE_THAT(zmax, Catch::Matchers::WithinAbs(10.0, 0.05));
        REQUIRE_THAT(zmin, Catch::Matchers::WithinAbs(-4.0, 0.05));
    }
}
