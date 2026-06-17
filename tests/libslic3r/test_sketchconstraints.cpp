#include <catch2/catch.hpp>
#include "libslic3r/SketchConstraints.hpp"

using namespace Slic3r;

TEST_CASE("Coincident with anchor", "[SketchConstraints]")
{
    SketchConstraints sc;
    int a = sc.add_point(0, 0);
    int b = sc.add_point(5, 5);
    sc.fix_point(a);
    sc.coincident(a, b);
    REQUIRE(sc.solve());
    Vec2d pb = sc.get_point(b);
    REQUIRE_THAT(pb.x(), Catch::Matchers::WithinAbs(0.0, 1e-4));
    REQUIRE_THAT(pb.y(), Catch::Matchers::WithinAbs(0.0, 1e-4));
}

TEST_CASE("Horizontal + distance", "[SketchConstraints]")
{
    SketchConstraints sc;
    int a = sc.add_point(0, 0);
    int b = sc.add_point(5, 3);
    sc.fix_point(a);
    sc.horizontal(a, b);
    sc.distance(a, b, 10);
    REQUIRE(sc.solve());
    Vec2d pb = sc.get_point(b);
    REQUIRE_THAT(pb.y(), Catch::Matchers::WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(std::abs(pb.x()), Catch::Matchers::WithinAbs(10.0, 1e-3));
}

TEST_CASE("Rectangle", "[SketchConstraints]")
{
    SketchConstraints sc;
    int p0 = sc.add_point(0, 0);
    int p1 = sc.add_point(8, 1);
    int p2 = sc.add_point(9, 5);
    int p3 = sc.add_point(-1, 4);
    sc.fix_point(p0);
    sc.lock_x(p0, 0);
    sc.lock_y(p0, 0);
    sc.horizontal(p0, p1);
    sc.vertical(p1, p2);
    sc.horizontal(p2, p3);
    sc.vertical(p3, p0);
    sc.distance(p0, p1, 10);
    sc.distance(p1, p2, 6);
    REQUIRE(sc.solve());
    Vec2d pp1 = sc.get_point(p1);
    Vec2d pp2 = sc.get_point(p2);
    Vec2d pp3 = sc.get_point(p3);
    REQUIRE_THAT(pp1.x(), Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(pp1.y(), Catch::Matchers::WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(pp2.x(), Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(pp2.y(), Catch::Matchers::WithinAbs(6.0, 1e-3));
    REQUIRE_THAT(pp3.x(), Catch::Matchers::WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(pp3.y(), Catch::Matchers::WithinAbs(6.0, 1e-3));
}

TEST_CASE("residual_norm after each solve", "[SketchConstraints]")
{
    SECTION("coincident case")
    {
        SketchConstraints sc;
        int a = sc.add_point(0, 0);
        int b = sc.add_point(5, 5);
        sc.fix_point(a);
        sc.coincident(a, b);
        REQUIRE(sc.solve());
        REQUIRE(sc.residual_norm() < 1e-5);
    }
    SECTION("horizontal+distance case")
    {
        SketchConstraints sc;
        int a = sc.add_point(0, 0);
        int b = sc.add_point(5, 3);
        sc.fix_point(a);
        sc.horizontal(a, b);
        sc.distance(a, b, 10);
        REQUIRE(sc.solve());
        REQUIRE(sc.residual_norm() < 1e-5);
    }
    SECTION("rectangle case")
    {
        SketchConstraints sc;
        int p0 = sc.add_point(0, 0);
        int p1 = sc.add_point(8, 1);
        int p2 = sc.add_point(9, 5);
        int p3 = sc.add_point(-1, 4);
        sc.fix_point(p0);
        sc.lock_x(p0, 0);
        sc.lock_y(p0, 0);
        sc.horizontal(p0, p1);
        sc.vertical(p1, p2);
        sc.horizontal(p2, p3);
        sc.vertical(p3, p0);
        sc.distance(p0, p1, 10);
        sc.distance(p1, p2, 6);
        REQUIRE(sc.solve());
        REQUIRE(sc.residual_norm() < 1e-5);
    }
}
