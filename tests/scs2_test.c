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

int main(void)
{
    test_engine();
    test_forward_gears();
    test_pedals();
    test_reverse();
    test_gear_text();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
