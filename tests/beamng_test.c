/*
 * Unit tests for BeamNG.drive in the OutGauge/OutSim mapper
 * (map_outgauge_outsim_data with SIMULATOREXE_BEAMNG).
 *
 * Each test builds a UDP packet in a zeroed buffer, as simd's receive buffer
 * is zeroed, and runs the real mapper on it.
 *
 * OutGauge fields follow BeamNG's protocol documentation. These tests only
 * verify the mapping arithmetic; how BeamNG fills each field in a given game
 * version still needs an in-game check.
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

#include "../include/outgauge.h"

// simd receives into a zeroed buffer far larger than any packet
#define PACKET_BUFFER 2048

static int failures = 0;
static int checks = 0;

static void check(const char* test, const char* what, double got, double want)
{
    checks++;
    // isnan() first: a NaN result would otherwise compare as a pass
    if (isnan(got) || fabs(got - want) > 1e-6)
    {
        failures++;
        fprintf(stderr, "FAIL %-16s %-30s got %f want %f\n", test, what, got, want);
    }
}

static void check_near(const char* test, const char* what, double got, double want, double tolerance)
{
    checks++;
    if (isnan(got) || fabs(got - want) > tolerance)
    {
        failures++;
        fprintf(stderr, "FAIL %-16s %-30s got %f want %f (+/- %f)\n", test, what, got, want, tolerance);
    }
}

static void check_str(const char* test, const char* what, const char* got, const char* want)
{
    checks++;
    if (strcmp(got, want) != 0)
    {
        failures++;
        fprintf(stderr, "FAIL %-16s %-30s got \"%s\" want \"%s\"\n", test, what, got, want);
    }
}

typedef struct
{
    SimData* sd;
    SimMap* sm;
    char* packet;
}
Fixture;

static Fixture fixture(void)
{
    Fixture f;
    f.sd = calloc(1, sizeof(SimData));
    f.sm = calloc(1, sizeof(SimMap));
    f.packet = calloc(1, PACKET_BUFFER);
    return f;
}

static void done(Fixture* f)
{
    free(f->sd);
    free(f->sm);
    free(f->packet);
}

// the mapper normally runs after simapi_datamap() has set car to "DEFAULT"
static void map(Fixture* f)
{
    strcpy(f->sd->car, "DEFAULT");
    map_outgauge_outsim_data(f->sd, f->sm, SIMULATOREXE_BEAMNG, f->packet);
}

/* ---------------- OutGauge ---------------- */

static struct outgauge* outgauge(Fixture* f)
{
    memset(f->packet, 0, PACKET_BUFFER);
    struct outgauge* og = (struct outgauge*) f->packet;
    memcpy(og->car, "beam", 4);   // BeamNG always sends "beam"
    return og;
}

static void test_outgauge_basics(void)
{
    Fixture f = fixture();
    struct outgauge* og = outgauge(&f);
    og->rpm = 3499.6f;
    og->speed = 25.0f;
    og->throttle = 0.25f;
    og->brake = 0.5f;
    og->gear = 3;
    map(&f);
    check("outgauge", "rpms rounded", f.sd->rpms, 3500);
    check("outgauge", "25 m/s -> 90 km/h", f.sd->velocity, 90);
    check("outgauge", "throttle", f.sd->gas, 0.25);
    check("outgauge", "brake", f.sd->brake, 0.5);
    check("outgauge", "gear 3 (2nd) as is", f.sd->gear, SIMAPI_GEAR_SECOND);
    done(&f);
}

static void test_car_name(void)
{
    Fixture f = fixture();
    outgauge(&f);
    map(&f);
    check_str("car", "beam", f.sd->car, "beam");
    done(&f);
}

int main(void)
{
    test_outgauge_basics();
    test_car_name();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
