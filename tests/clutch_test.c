/*
 * Unit tests for simdata->clutch.
 *
 * Each test feeds one sim's mapper a zeroed raw buffer with only the clutch
 * field (and whatever the mapper needs to route the packet) set, then checks
 * the value the mapper writes to simdata->clutch.
 *
 * Convention under test: 0.0 = pedal released, 1.0 = pedal fully pressed,
 * clamped to 0.0..1.0, 0.0 when there is no clutch data.
 *
 * These tests only verify the mapping arithmetic. They say nothing about
 * which direction a game actually reports; that needs an in-game check.
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
#include "../include/forza.h"

static int failures = 0;
static int checks = 0;

static void check(const char* sim, const char* what, double got, double want)
{
    checks++;
    // isnan() first: a NaN result would otherwise compare as a pass
    if (isnan(got) || fabs(got - want) > 1e-6)
    {
        failures++;
        fprintf(stderr, "FAIL %-12s %-24s got %f want %f\n", sim, what, got, want);
    }
}

static SimData* new_simdata(void)
{
    return calloc(1, sizeof(SimData));
}

static SimMap* new_simmap(void)
{
    return calloc(1, sizeof(SimMap));
}

/* ---------------- shared memory sims ---------------- */

static double ac_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct SPageFilePhysics* p = calloc(1, sizeof(*p));
    p->clutch = raw;
    sm->ac.physics_map_addr = p;

    map_assetto_corsa_data(sd, sm, SIMULATOREXE_ASSETTO_CORSA);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double rf2_clutch(double raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct rF2Telemetry* t = calloc(1, sizeof(*t));
    t->mVehicles[0].mUnfilteredClutch = raw;
    sm->rf2.telemetry_map_addr = t;

    map_rfactor2_data(sd, sm);
    double c = sd->clutch;

    free(t);
    free(sm);
    free(sd);
    return c;
}

static double pcars2_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct pcars2APIStruct* t = calloc(1, sizeof(*t));
    t->mUnfilteredClutch = raw;
    sm->pcars2.telemetry_map_addr = t;

    map_project_cars2_data(sd, sm, false, NULL);
    double c = sd->clutch;

    free(t);
    free(sm);
    free(sd);
    return c;
}

static double scs2_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct scs2TelemetryMap_s* t = calloc(1, sizeof(*t));
    t->truck_f.userClutch = raw;
    sm->scs2.telemetry_map_addr = t;

    map_trucks_data(sd, sm);
    double c = sd->clutch;

    free(t);
    free(sm);
    free(sd);
    return c;
}

static double r3e_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct r3e_share* t = calloc(1, sizeof(*t));
    t->clutch = raw;
    sm->r3e.telemetry_map_addr = t;

    map_r3e_data(sd, sm);
    double c = sd->clutch;

    free(t);
    free(sm);
    free(sd);
    return c;
}

/* ---------------- UDP sims ---------------- */

static double ams2udp_clutch(uint8_t raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct ams2UDPData* p = calloc(1, sizeof(*p));
    p->mPacketType = 0; // car physics
    p->sUnfilteredClutch = raw;

    map_project_cars2_data(sd, sm, true, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double dirt2_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct dirt2_udp_packet* p = calloc(1, sizeof(*p));
    p->fields.clutch = raw;

    map_dirt_rally_2_data(sd, sm, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double rbr_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    RBR_TelemetryData* p = calloc(1, sizeof(*p));
    p->control_.clutch_ = raw;

    map_richard_burns_rally_data(sd, sm, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double wf2_clutch(float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    WF2_PacketMain* p = calloc(1, sizeof(*p));
    p->header.signature = 1869769584;
    p->header.packetType = WF2_PACKET_TYPE_MAIN;
    p->carPlayer.input.clutch = raw;

    map_wreckfest2_data(sd, sm, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double outgauge_clutch(SimulatorEXE simexe, int id, float raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    struct outgauge* p = calloc(1, sizeof(*p));
    p->id = id;
    p->clutch = raw;

    map_outgauge_outsim_data(sd, sm, simexe, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

static double beamng_clutch(float raw)
{
    return outgauge_clutch(SIMULATOREXE_BEAMNG, 0, raw);
}

static double lfs_clutch(float raw)
{
    // LFS OutGauge packets are recognised by OutGauge ID 2
    return outgauge_clutch(SIMULATOREXE_LIVE_FOR_SPEED, 2, raw);
}

static double forza_clutch(uint8_t raw)
{
    SimData* sd = new_simdata();
    SimMap* sm = new_simmap();
    Forza_Dash* p = calloc(1, sizeof(*p));
    p->Clutch = raw;

    map_forza_data(sd, sm, (char*) p);
    double c = sd->clutch;

    free(p);
    free(sm);
    free(sd);
    return c;
}

/* ---------------- tests ---------------- */

static void test_float_range(const char* sim, double (*f)(float))
{
    check(sim, "released 0.0", f(0.0f), 0.0);
    check(sim, "pressed 1.0", f(1.0f), 1.0);
    check(sim, "midpoint 0.5", f(0.5f), 0.5);
}

static void test_float_clamp(const char* sim, double (*f)(float))
{
    check(sim, "above range 1.5", f(1.5f), 1.0);
    check(sim, "below range -0.2", f(-0.2f), 0.0);
    check(sim, "NaN", f(NAN), 0.0);
}

static void test_byte_range(const char* sim, double (*f)(uint8_t))
{
    check(sim, "released 0", f(0), 0.0);
    check(sim, "pressed 255", f(255), 1.0);
    check(sim, "midpoint 51", f(51), 0.2);
}

static void test_double_range(const char* sim, double (*f)(double))
{
    check(sim, "released 0.0", f(0.0), 0.0);
    check(sim, "pressed 1.0", f(1.0), 1.0);
    check(sim, "midpoint 0.5", f(0.5), 0.5);
}

int main(void)
{
    test_float_range("ac", ac_clutch);
    test_double_range("rf2", rf2_clutch);
    test_float_range("pcars2", pcars2_clutch);
    test_float_clamp("pcars2", pcars2_clutch);
    test_byte_range("ams2 udp", ams2udp_clutch);
    test_float_range("scs2", scs2_clutch);
    test_float_clamp("scs2", scs2_clutch);
    test_float_range("r3e", r3e_clutch);
    test_float_clamp("r3e", r3e_clutch);
    check("r3e", "not available -1.0", r3e_clutch(-1.0f), 0.0);
    test_float_range("beamng", beamng_clutch);
    test_float_clamp("beamng", beamng_clutch);
    test_float_range("lfs", lfs_clutch);
    test_byte_range("forza", forza_clutch);
    test_float_range("dirt2", dirt2_clutch);
    test_float_range("rbr", rbr_clutch);
    test_float_range("wreckfest2", wf2_clutch);

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
