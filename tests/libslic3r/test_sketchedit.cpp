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
