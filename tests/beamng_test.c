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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

static void test_clutch(void)
{
    Fixture f = fixture();
    struct outgauge* og = outgauge(&f);
    map(&f);
    check("clutch", "released", f.sd->clutch, 0.0);
    og->clutch = 0.6f;
    map(&f);
    check("clutch", "0.6", f.sd->clutch, 0.6f);
    og->clutch = 1.0f;
    map(&f);
    check("clutch", "fully pressed", f.sd->clutch, 1.0);
    done(&f);
}

static void test_gear_text(void)
{
    Fixture f = fixture();
    struct outgauge* og = outgauge(&f);
    og->gear = 0;
    map(&f);
    check("gearc", "reverse gear", f.sd->gear, SIMAPI_GEAR_REVERSE);
    check_str("gearc", "R", f.sd->gearc, "R");
    og->gear = 1;
    map(&f);
    check_str("gearc", "N", f.sd->gearc, "N");
    og->gear = 2;
    map(&f);
    check_str("gearc", "1", f.sd->gearc, "1");
    og->gear = 7;
    map(&f);
    check_str("gearc", "6", f.sd->gearc, "6");
    done(&f);
}

static void test_handbrake(void)
{
    Fixture f = fixture();
    struct outgauge* og = outgauge(&f);
    og->dashLights = OG_DL_HANDBRAKE | OG_DL_ABS | OG_DL_TC;
    og->showLights = OG_DL_ABS | OG_DL_TC | OG_DL_SIGNAL_L;
    map(&f);
    check("handbrake", "light off -> 0", f.sd->handbrake, 0.0);
    og->showLights |= OG_DL_HANDBRAKE;
    map(&f);
    check("handbrake", "light on -> 1", f.sd->handbrake, 1.0);
    done(&f);
}

/* ---------------- MotionSim ---------------- */

_Static_assert(sizeof(struct beamng_motionsim) == 88, "BeamNG MotionSim packets are 88 bytes");

static struct beamng_motionsim* motionsim(Fixture* f)
{
    memset(f->packet, 0, PACKET_BUFFER);
    struct beamng_motionsim* ms = (struct beamng_motionsim*) f->packet;
    memcpy(ms->magic, "BNG1", 4);
    ms->upZ = 1.0f;   // level car
    return ms;
}

static double deg(double d)
{
    return d * M_PI / 180.0;
}

static void test_motionsim_axes(void)
{
    Fixture f = fixture();
    struct beamng_motionsim* ms = motionsim(&f);

    // facing +Y (yaw 0), moving forwards
    ms->velY = 10.0f;
    map(&f);
    check_near("motionsim", "yaw 0, forwards: Y", f.sd->Yvelocity, 10.0, 1e-5);
    check_near("motionsim", "yaw 0, forwards: X", f.sd->Xvelocity, 0.0, 1e-5);
    check_near("motionsim", "yaw 0, forwards: Z", f.sd->Zvelocity, 0.0, 1e-5);

    // facing +X (yaw 90, clockwise from +Y), moving forwards
    ms->yawPos = deg(90);
    ms->velX = 10.0f;
    ms->velY = 0.0f;
    map(&f);
    check_near("motionsim", "yaw 90, forwards: Y", f.sd->Yvelocity, 10.0, 1e-5);
    check_near("motionsim", "yaw 90, forwards: X", f.sd->Xvelocity, 0.0, 1e-5);
    check_near("motionsim", "heading 90", f.sd->heading, 90.0, 1e-4);

    // facing +Y, sliding towards +X: that's the car's right
    ms->yawPos = 0.0f;
    ms->velX = 3.0f;
    map(&f);
    check_near("motionsim", "sideways right: X", f.sd->Xvelocity, 3.0, 1e-5);
    check_near("motionsim", "sideways right: Y", f.sd->Yvelocity, 0.0, 1e-5);

    // rising
    ms->velX = 0.0f;
    ms->velZ = 2.0f;
    map(&f);
    check_near("motionsim", "rising: Z", f.sd->Zvelocity, 2.0, 1e-5);

    // world values pass through, z stays vertical
    ms->posX = 100.0f;
    ms->posY = -200.0f;
    ms->posZ = 30.0f;
    ms->velX = 1.0f;
    ms->velY = 2.0f;
    map(&f);
    check("motionsim", "worldposx", f.sd->worldposx, 100.0);
    check("motionsim", "worldposy", f.sd->worldposy, -200.0);
    check("motionsim", "worldposz (up)", f.sd->worldposz, 30.0);
    check("motionsim", "worldXvelocity", f.sd->worldXvelocity, 1.0);
    check("motionsim", "worldYvelocity", f.sd->worldYvelocity, 2.0);
    check("motionsim", "worldZvelocity (up)", f.sd->worldZvelocity, 2.0);
    done(&f);
}

/*
 * Real BeamNG MotionSim packets, from the Wireshark captures published in
 * github.com/joesphan/Fullmotion-Racing-Sim ("BeamNG.drive data decoding.md").
 * Expected values were decoded independently (Python) for this test.
 */
static const unsigned char capture_horizontal_g[88] = { 0x42, 0x4e, 0x47, 0x31, 0x7b, 0xa0, 0x12, 0x44, 0x60, 0x80, 0x23, 0xc3, 0xe7, 0x32, 0x5e, 0x3e, 0x5b, 0xb7, 0xbf, 0xc0, 0x60, 0x53, 0x3c, 0xc1, 0x58, 0xdd, 0x97, 0xbb, 0x51, 0x1e, 0xed, 0x40, 0x40, 0x27, 0xfe, 0xbd, 0xa5, 0x77, 0x8c, 0xbe, 0x3c, 0x63, 0x0d, 0x3d, 0x0a, 0xe1, 0xb7, 0xbc, 0x6c, 0xc8, 0x7f, 0x3f, 0x9a, 0x5e, 0x27, 0xbd, 0x4a, 0xf1, 0x9f, 0xbb, 0x57, 0x33, 0x2c, 0xc0, 0x6b, 0x89, 0x12, 0xbd, 0x92, 0x00, 0xb6, 0xbd, 0x98, 0x32, 0x19, 0x3f, 0x9d, 0xe8, 0x11, 0xc2, 0x08, 0x8b, 0xe6, 0xc2, 0x48, 0x7b, 0x84, 0x41 };
static const double expect_horizontal_g[] = { /* X, Y, Z velocity */ 0.2646, 13.2046, 0.0528, /* heading, pitch, roll */ -154.1619, -0.2797, -2.3412, /* world x y z */ 586.5075, -163.5015, 0.2170 };
static const unsigned char capture_forward_g[88] = { 0x42, 0x4e, 0x47, 0x31, 0x68, 0xc2, 0xa9, 0xc4, 0xfe, 0x6f, 0xbc, 0x43, 0xc0, 0x21, 0xda, 0x3d, 0x7e, 0x03, 0x2e, 0xc2, 0xd1, 0x12, 0xa2, 0x3f, 0x48, 0x2c, 0x95, 0xbb, 0x57, 0x27, 0x99, 0xbe, 0x51, 0x7b, 0x2e, 0xc1, 0xe9, 0x8f, 0xf3, 0xbe, 0xf0, 0x0a, 0xff, 0xbc, 0x7d, 0xb1, 0xaa, 0xb9, 0x3b, 0xe0, 0x7f, 0x3f, 0xc0, 0x3d, 0x92, 0xba, 0x37, 0x73, 0xfb, 0xbc, 0x00, 0xd0, 0xc5, 0xbf, 0x96, 0xa2, 0xa1, 0x3b, 0x3d, 0xd6, 0x48, 0xbd, 0xd3, 0x5c, 0x08, 0xbc, 0x95, 0xa4, 0x0a, 0x42, 0x45, 0x34, 0xb1, 0xc2, 0x8a, 0x7f, 0x6e, 0xc1 };
static const double expect_forward_g[] = { /* X, Y, Z velocity */ 0.1630, 43.5012, 1.3494, /* heading, pitch, roll */ -88.5455, -1.7587, -0.0639, /* world x y z */ -1358.0752, 376.8749, 0.1065 };
static const unsigned char capture_vertical_g[88] = { 0x42, 0x4e, 0x47, 0x31, 0x97, 0x4f, 0x5b, 0x43, 0xd5, 0x0c, 0xb1, 0xc3, 0xf8, 0x5a, 0x28, 0x44, 0x48, 0xef, 0xa2, 0xbc, 0x4c, 0x7c, 0x93, 0x42, 0x0b, 0x06, 0x2f, 0xc2, 0x4e, 0x56, 0xb8, 0x3e, 0xf1, 0x18, 0xa2, 0x40, 0xf3, 0xee, 0x01, 0xc1, 0x3f, 0x0c, 0x57, 0x3d, 0x81, 0x8e, 0x25, 0x3f, 0x58, 0xcc, 0x42, 0x3f, 0x87, 0xac, 0x08, 0x3d, 0x27, 0x95, 0x34, 0xbf, 0xb3, 0x33, 0xf2, 0x3c, 0x59, 0x9c, 0x7e, 0x3c, 0x24, 0x47, 0x84, 0xbe, 0x73, 0x31, 0x4d, 0x3c, 0x56, 0x26, 0x17, 0xbe, 0x87, 0x75, 0xc0, 0xbf, 0xd3, 0xfe, 0x2e, 0x3d };
static const double expect_vertical_g[] = { /* X, Y, Z velocity */ -2.6815, 84.4879, 14.3936, /* heading, pitch, roll */ 1.6940, -40.4165, 1.9118, /* world x y z */ 219.3109, -354.1003, 673.4214 };
static const unsigned char capture_roll[88] = { 0x42, 0x4e, 0x47, 0x31, 0x52, 0x6f, 0x18, 0x43, 0x64, 0x80, 0x26, 0xc2, 0xfb, 0x8b, 0x41, 0x40, 0x98, 0x9f, 0x1f, 0x39, 0x94, 0xa5, 0x30, 0x39, 0x99, 0x6b, 0x57, 0x38, 0xf0, 0x25, 0xf6, 0x3a, 0x4c, 0x59, 0xcb, 0x39, 0x58, 0x47, 0x54, 0x3a, 0xb9, 0x67, 0xef, 0xbe, 0x31, 0xbd, 0x6f, 0xbc, 0x60, 0x42, 0x62, 0x3f, 0x2e, 0x12, 0xf8, 0xbe, 0x3c, 0xbe, 0x4a, 0xbd, 0xdd, 0x59, 0xff, 0xbd, 0xfc, 0xbd, 0x25, 0x37, 0x72, 0xa3, 0xfb, 0x37, 0x03, 0x82, 0x06, 0x39, 0x2a, 0x00, 0xc3, 0x3d, 0xe4, 0xe6, 0x24, 0xbe, 0x5a, 0x2a, 0x6c, 0x3e };
static const double expect_roll[] = { /* X, Y, Z velocity */ 0.0002, 0.0001, -0.0000, /* heading, pitch, roll */ -7.1438, -2.8360, -27.7606, /* world x y z */ 152.4348, -41.6254, 3.0242 };

static void check_capture(const char* name, const unsigned char* capture, const double* want)
{
    Fixture f = fixture();
    memcpy(f.packet, capture, 88);
    map(&f);
    const char* what[] = { "Xvelocity", "Yvelocity", "Zvelocity", "heading", "pitch", "roll", "worldposx", "worldposy", "worldposz" };
    double got[] = { f.sd->Xvelocity, f.sd->Yvelocity, f.sd->Zvelocity, f.sd->heading, f.sd->pitch, f.sd->roll, f.sd->worldposx, f.sd->worldposy, f.sd->worldposz };
    for (int i = 0; i < 9; i++)
    {
        check_near(name, what[i], got[i], want[i], 0.01);
    }
    done(&f);
}

static void test_motionsim_captures(void)
{
    check_capture("capture horiz G", capture_horizontal_g, expect_horizontal_g);
    check_capture("capture fwd G", capture_forward_g, expect_forward_g);
    check_capture("capture vert G", capture_vertical_g, expect_vertical_g);
    check_capture("capture roll", capture_roll, expect_roll);
}

// OutGauge and MotionSim packets arrive interleaved on one port; each must
// leave the other's fields alone
static void test_dispatch(void)
{
    Fixture f = fixture();
    struct outgauge* og = outgauge(&f);
    og->rpm = 2000.0f;
    og->gear = 2;
    map(&f);

    struct beamng_motionsim* ms = motionsim(&f);
    ms->velY = 20.0f;
    map(&f);
    check("dispatch", "MotionSim keeps rpm", f.sd->rpms, 2000);
    check("dispatch", "MotionSim keeps gear", f.sd->gear, SIMAPI_GEAR_FIRST);
    check_near("dispatch", "MotionSim sets Yvelocity", f.sd->Yvelocity, 20.0, 1e-5);

    og = outgauge(&f);
    og->rpm = 2500.0f;
    map(&f);
    check("dispatch", "OutGauge sets rpm", f.sd->rpms, 2500);
    check_near("dispatch", "OutGauge keeps Yvelocity", f.sd->Yvelocity, 20.0, 1e-5);
    done(&f);
}

// LFS keeps its own handling; a packet starting with "BNG1" isn't MotionSim there
static void test_lfs_ignores_magic(void)
{
    Fixture f = fixture();
    struct beamng_motionsim* ms = motionsim(&f);
    ms->velY = 20.0f;
    strcpy(f.sd->car, "DEFAULT");
    map_outgauge_outsim_data(f.sd, f.sm, SIMULATOREXE_LIVE_FOR_SPEED, f.packet);
    check("lfs", "no MotionSim parsing", f.sd->Yvelocity, 0.0);
    done(&f);
}

int main(void)
{
    test_outgauge_basics();
    test_car_name();
    test_clutch();
    test_gear_text();
    test_handbrake();
    test_motionsim_axes();
    test_motionsim_captures();
    test_dispatch();
    test_lfs_ignores_magic();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
