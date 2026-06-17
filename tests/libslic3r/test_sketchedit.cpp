#include <catch2/catch.hpp>
#include "libslic3r/SketchEngine.hpp"
#include <cmath>

using namespace Slic3r;

using Catch::Matchers::WithinAbs;

TEST_CASE("Mirror Line across Y axis", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(3, 2);
    e.p1   = Vec2d(5, 4);

    Vec2d a(0, -1);
    Vec2d b(0, 1);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Line);
    REQUIRE_THAT(m.p0.x(), WithinAbs(-3.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(m.p1.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.p1.y(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("Mirror Circle across Y axis", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(5, 0);
    e.p0     = Vec2d(5, 0);
    e.radius = 3;

    Vec2d a(0, -1);
    Vec2d b(0, 1);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Circle);
    REQUIRE_THAT(m.center.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.center.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(m.radius, WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(m.p0.x(), WithinAbs(-5.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Mirror Arc across X axis", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 1.0;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;
    e.p0          = Vec2d(1, 0);
    e.p1          = Vec2d(0, 1);

    Vec2d a(-1, 0);
    Vec2d b(1, 0);

    auto result = SketchEngine::mirror_entities({e}, a, b);
    REQUIRE(result.size() == 1);

    const auto& m = result[0];
    REQUIRE(m.type == SketchEntity::Type::Arc);

    REQUIRE_THAT(m.p0.x(), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(m.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(m.p1.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(m.p1.y(), WithinAbs(-1.0, 1e-9));

    double sweep = m.end_angle - m.start_angle;
    double orig_sweep = e.end_angle - e.start_angle;
    REQUIRE(orig_sweep > 0.0);
    REQUIRE(sweep < 0.0);
}

TEST_CASE("Offset Line by positive d", "[SketchEdit]")
{
    SketchEntity e;
    e.type = SketchEntity::Type::Line;
    e.p0   = Vec2d(0, 0);
    e.p1   = Vec2d(10, 0);

    auto result = SketchEngine::offset_entities({e}, 2.0);
    REQUIRE(result.size() == 1);

    const auto& o = result[0];
    REQUIRE(o.type == SketchEntity::Type::Line);
    REQUIRE_THAT(o.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p0.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(o.p1.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(o.p1.y(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Offset Circle: expand and collapse", "[SketchEdit]")
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = Vec2d(0, 0);
    e.p0     = Vec2d(0, 0);
    e.radius = 5;

    auto expanded = SketchEngine::offset_entities({e}, 2.0);
    REQUIRE(expanded.size() == 1);
    REQUIRE_THAT(expanded[0].radius, WithinAbs(7.0, 1e-9));

    auto collapsed = SketchEngine::offset_entities({e}, -5.0);
    REQUIRE(collapsed.empty());
}

TEST_CASE("Offset Arc by positive d", "[SketchEdit]")
{
    SketchEntity e;
    e.type        = SketchEntity::Type::Arc;
    e.center      = Vec2d(0, 0);
    e.radius      = 4.0;
    e.start_angle = 0.0;
    e.end_angle   = M_PI / 2.0;
    e.p0          = Vec2d(4, 0);
    e.p1          = Vec2d(0, 4);

    auto result = SketchEngine::offset_entities({e}, 1.0);
    REQUIRE(result.size() == 1);

    const auto& o = result[0];
    REQUIRE(o.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(o.radius, WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(o.p0.x(), WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(o.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p1.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(o.p1.y(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Fillet right-angle corner", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(10, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(10, 0);
    b.p1   = Vec2d(10, 10);

    SketchEntity a_out, b_out, arc_out;
    bool ok = SketchEngine::fillet_lines(a, b, 2.0, a_out, b_out, arc_out);
    REQUIRE(ok);

    REQUIRE_THAT(a_out.p0.x(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(a_out.p0.y(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(a_out.p1.x(), WithinAbs(8.0, 1e-9));
    REQUIRE_THAT(a_out.p1.y(), WithinAbs(0.0, 1e-9));

    REQUIRE_THAT(b_out.p0.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(b_out.p0.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(b_out.p1.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(b_out.p1.y(), WithinAbs(10.0, 1e-9));

    REQUIRE(arc_out.type == SketchEntity::Type::Arc);
    REQUIRE_THAT(arc_out.radius, WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(arc_out.center.x(), WithinAbs(8.0, 1e-9));
    REQUIRE_THAT(arc_out.center.y(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT((arc_out.p0 - arc_out.center).norm(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT((arc_out.p1 - arc_out.center).norm(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Fillet parallel lines returns false", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(10, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(0, 5);
    b.p1   = Vec2d(10, 5);

    SketchEntity a_out, b_out, arc_out;
    REQUIRE_FALSE(SketchEngine::fillet_lines(a, b, 1.0, a_out, b_out, arc_out));
}

TEST_CASE("Fillet arc too big returns false", "[SketchEdit]")
{
    SketchEntity a;
    a.type = SketchEntity::Type::Line;
    a.p0   = Vec2d(0, 0);
    a.p1   = Vec2d(1, 0);

    SketchEntity b;
    b.type = SketchEntity::Type::Line;
    b.p0   = Vec2d(1, 0);
    b.p1   = Vec2d(1, 1);

    SketchEntity a_out, b_out, arc_out;
    REQUIRE_FALSE(SketchEngine::fillet_lines(a, b, 5.0, a_out, b_out, arc_out));
}
