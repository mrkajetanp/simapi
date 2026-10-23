/*
 * Unit tests for the ETS2 / ATS mapper (map_trucks_data).
 *
 * Each test fills a zeroed scs-sdk-plugin shared memory struct with only the
 * fields under test, points simmap->scs2.telemetry_map_addr at it, runs the
 * real mapper and checks what lands in SimData.
 *
 * Units, ranges and axes come from the SCS telemetry SDK headers. These tests
 * only verify the mapping arithmetic; signs the SDK doesn't state (e.g.
 * suspension deflection) still need an in-game check.
 *
 * Build and run:
 *   cmake -B build -DENABLE_TESTS=ON && cmake --build build && ctest --test-dir build
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../simapi/simapi.h"
#include "../simapi/simdata.h"
#include "../simapi/simmapper.h"
#include "../simapi/simmap.h"

#include "../include/scs2data.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
static int checks = 0;

static void check(const char* test, const char* what, double got, double want)
{
    checks++;
    // isnan() first: a NaN result would otherwise compare as a pass
    if (isnan(got) || fabs(got - want) > 1e-6)
    {
        failures++;
        fprintf(stderr, "FAIL %-16s %-28s got %f want %f\n", test, what, got, want);
    }
}

static void check_str(const char* test, const char* what, const char* got, const char* want)
{
    checks++;
    if (strcmp(got, want) != 0)
    {
        failures++;
        fprintf(stderr, "FAIL %-16s %-28s got \"%s\" want \"%s\"\n", test, what, got, want);
    }
}

typedef struct
{
    SimData* sd;
    SimMap* sm;
    struct scs2TelemetryMap_s* raw;
}
Fixture;

static Fixture fixture(void)
{
    Fixture f;
    f.sd = calloc(1, sizeof(SimData));
    f.sm = calloc(1, sizeof(SimMap));
    f.raw = calloc(1, sizeof(struct scs2TelemetryMap_s));
    f.sm->scs2.telemetry_map_addr = f.raw;
    f.raw->sdkActive = true;
    f.raw->paused = false;
    return f;
}

static void map(Fixture* f)
{
    map_trucks_data(f->sd, f->sm);
}

static void done(Fixture* f)
{
    free(f->sd);
    free(f->sm);
    free(f->raw);
}

static void test_engine(void)
{
    Fixture f = fixture();
    f.raw->truck_f.engineRpm = 1199.6f;
    f.raw->config_f.engineRpmMax = 2500.0f;
    map(&f);
    check("engine", "rpms rounded", f.sd->rpms, 1200);
    check("engine", "maxrpm", f.sd->maxrpm, 2500);
    done(&f);
}

static void test_forward_gears(void)
{
    Fixture f = fixture();
    f.raw->truck_i.gear = 0;
    map(&f);
    check("gear", "neutral -> 1", f.sd->gear, SIMAPI_GEAR_NEUTRAL);

    f.raw->truck_i.gear = 1;
    map(&f);
    check("gear", "first -> 2", f.sd->gear, SIMAPI_GEAR_FIRST);

    f.raw->truck_i.gear = 12;
    map(&f);
    check("gear", "twelfth -> 13", f.sd->gear, 13);
    done(&f);
}

static void test_pedals(void)
{
    Fixture f = fixture();
    f.raw->truck_f.userThrottle = 0.3f;
    f.raw->truck_f.userBrake = 0.7f;
    map(&f);
    check("pedals", "throttle 0.3", f.sd->gas, 0.3f);
    check("pedals", "brake 0.7", f.sd->brake, 0.7f);

    f.raw->truck_f.userThrottle = 1.0f;
    f.raw->truck_f.userBrake = 0.0f;
    map(&f);
    check("pedals", "throttle 1", f.sd->gas, 1.0);
    check("pedals", "brake 0", f.sd->brake, 0.0);
    done(&f);
}

static void test_reverse(void)
{
    Fixture f = fixture();
    f.raw->config_ui.gears_reverse = 2;
    f.raw->truck_f.speed = -5.0f;
    f.raw->truck_i.gear = -1;
    f.raw->truck_i.gearDashboard = -1;
    map(&f);
    check("reverse", "speed -5 m/s -> 18 km/h", f.sd->velocity, 18);
    check("reverse", "R1 -> reverse", f.sd->gear, SIMAPI_GEAR_REVERSE);
    check_str("reverse", "gearc R1", f.sd->gearc, "R1");

    f.raw->truck_i.gear = -2;
    f.raw->truck_i.gearDashboard = -2;
    map(&f);
    check("reverse", "R2 -> reverse", f.sd->gear, SIMAPI_GEAR_REVERSE);
    check_str("reverse", "gearc R2", f.sd->gearc, "R2");

    f.raw->config_ui.gears_reverse = 1;
    f.raw->truck_i.gearDashboard = -1;
    map(&f);
    check_str("reverse", "gearc R (one reverse)", f.sd->gearc, "R");

    f.raw->truck_f.speed = 5.0f;
    map(&f);
    check("reverse", "speed +5 m/s -> 18 km/h", f.sd->velocity, 18);
    done(&f);
}

static void test_gear_text(void)
{
    Fixture f = fixture();
    f.raw->truck_i.gearDashboard = 0;
    map(&f);
    check_str("gearc", "neutral", f.sd->gearc, "N");

    f.raw->truck_i.gearDashboard = 5;
    map(&f);
    check_str("gearc", "5", f.sd->gearc, "5");

    f.raw->truck_i.gearDashboard = 12;
    map(&f);
    check_str("gearc", "12", f.sd->gearc, "12");
    done(&f);
}

static void set_wheel(Fixture* f, int i, float x, float z, bool powered, bool simulated)
{
    f->raw->config_fv.truckWheelPositionX[i] = x;
    f->raw->config_fv.truckWheelPositionZ[i] = z;
    f->raw->config_b.truckWheelPowered[i] = powered;
    f->raw->config_b.truckWheelSimulated[i] = simulated;
    // distinct per-wheel values so the test can tell which wheel was picked
    f->raw->truck_f.truck_wheelVelocity[i] = i + 1;
    f->raw->config_f.truckWheelRadius[i] = 0.5f + 0.01f * i;
    f->raw->truck_f.truck_wheelSuspDeflection[i] = 0.01f * (i + 1);
}

static void check_wheels(const char* test, Fixture* f, int fl, int fr, int rl, int rr)
{
    int want[4] = { fl, fr, rl, rr };
    const char* names[4] = { "front left", "front right", "rear left", "rear right" };
    char what[64];
    for (int i = 0; i < 4; i++)
    {
        snprintf(what, sizeof(what), "%s rad/s", names[i]);
        check(test, what, f->sd->tyreRPS[i], 2.0 * M_PI * (want[i] + 1));
        snprintf(what, sizeof(what), "%s diameter", names[i]);
        check(test, what, f->sd->tyrediameter[i], 2.0 * (0.5f + 0.01f * want[i]));
        snprintf(what, sizeof(what), "%s suspension", names[i]);
        check(test, what, f->sd->suspension[i], 0.01f * (want[i] + 1));
    }
}

// 6x4 tractor with the wheel indexes deliberately out of order
static void test_wheels_by_position(void)
{
    Fixture f = fixture();
    f.raw->config_ui.truckWheelCount = 6;
    set_wheel(&f, 0, -0.9f, 2.6f, true, true);   // second drive axle, left
    set_wheel(&f, 1, -1.0f, -3.6f, false, true); // front left
    set_wheel(&f, 2, 0.9f, 1.3f, true, true);    // first drive axle, right
    set_wheel(&f, 3, 1.0f, -3.6f, false, true);  // front right
    set_wheel(&f, 4, -0.9f, 1.3f, true, true);   // first drive axle, left
    set_wheel(&f, 5, 0.9f, 2.6f, true, true);    // second drive axle, right
    map(&f);
    check_wheels("wheels 6x4", &f, 1, 3, 4, 2);
    done(&f);
}

// 6x2 with a non-driven pusher axle ahead of the drive axle
static void test_wheels_pusher_axle(void)
{
    Fixture f = fixture();
    f.raw->config_ui.truckWheelCount = 6;
    set_wheel(&f, 0, -1.0f, -3.6f, false, true);
    set_wheel(&f, 1, 1.0f, -3.6f, false, true);
    set_wheel(&f, 2, -0.9f, 0.8f, false, true);
    set_wheel(&f, 3, 0.9f, 0.8f, false, true);
    set_wheel(&f, 4, -0.9f, 2.1f, true, true);
    set_wheel(&f, 5, 0.9f, 2.1f, true, true);
    map(&f);
    check_wheels("wheels pusher", &f, 0, 1, 4, 5);
    done(&f);
}

// a wheel the game doesn't simulate is never picked, even if it's furthest forward
static void test_wheels_unsimulated(void)
{
    Fixture f = fixture();
    f.raw->config_ui.truckWheelCount = 5;
    set_wheel(&f, 0, -1.0f, -5.0f, false, false);
    set_wheel(&f, 1, -1.0f, -3.6f, false, true);
    set_wheel(&f, 2, 1.0f, -3.6f, false, true);
    set_wheel(&f, 3, -0.9f, 1.3f, true, true);
    set_wheel(&f, 4, 0.9f, 1.3f, true, true);
    map(&f);
    check_wheels("wheels unsim", &f, 1, 2, 3, 4);
    done(&f);
}

// with no powered flags, the rear axle is the next one back
static void test_wheels_no_powered_flags(void)
{
    Fixture f = fixture();
    f.raw->config_ui.truckWheelCount = 6;
    set_wheel(&f, 0, -1.0f, -3.6f, false, true);
    set_wheel(&f, 1, 1.0f, -3.6f, false, true);
    set_wheel(&f, 2, -0.9f, 2.6f, false, true);
    set_wheel(&f, 3, 0.9f, 2.6f, false, true);
    set_wheel(&f, 4, -0.9f, 1.3f, false, true);
    set_wheel(&f, 5, 0.9f, 1.3f, false, true);
    map(&f);
    check_wheels("wheels unpowered", &f, 0, 1, 4, 5);
    done(&f);
}

// before the truck configuration arrives, wheels 0..3 are used as they are
static void test_wheels_no_config(void)
{
    Fixture f = fixture();
    for (int i = 0; i < 4; i++)
    {
        set_wheel(&f, i, 0.0f, 0.0f, false, false);
    }
    f.raw->config_ui.truckWheelCount = 0;
    map(&f);
    check_wheels("wheels noconfig", &f, 0, 1, 2, 3);
    done(&f);
}

int main(void)
{
    test_engine();
    test_forward_gears();
    test_pedals();
    test_reverse();
    test_gear_text();
    test_wheels_by_position();
    test_wheels_pusher_axle();
    test_wheels_unsimulated();
    test_wheels_no_powered_flags();
    test_wheels_no_config();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
