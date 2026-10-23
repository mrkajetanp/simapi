#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../simmap.h"
#include "../simdata.h"
#include "../simapi.h"
#include "../simmapper.h"
#include "../outgauge.h"

#include "../../include/outgauge.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int froundint(float f)
{
    return trunc(nearbyint(f));
}

/*
 * BeamNG MotionSim: world position, velocity and orientation. Velocity
 * arrives in world space (right-handed, Z up); project it onto the car's
 * right, forward and up directions for the local Xvelocity, Yvelocity and
 * Zvelocity. Forward comes from yaw (clockwise from +Y) and pitch (nose up
 * positive); up is sent in the packet.
 */
static void map_beamng_motionsim(SimData* simdata, char* a)
{
    double yaw = *(float*) (char*) (a + offsetof(struct beamng_motionsim, yawPos));
    double pitch = *(float*) (char*) (a + offsetof(struct beamng_motionsim, pitchPos));
    double roll = *(float*) (char*) (a + offsetof(struct beamng_motionsim, rollPos));

    double velocity[3];
    velocity[0] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, velX));
    velocity[1] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, velY));
    velocity[2] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, velZ));

    double up[3];
    up[0] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, upX));
    up[1] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, upY));
    up[2] = *(float*) (char*) (a + offsetof(struct beamng_motionsim, upZ));

    double forward[3];
    forward[0] = sin(yaw) * cos(pitch);
    forward[1] = cos(yaw) * cos(pitch);
    forward[2] = sin(pitch);

    // right = forward x up
    double right[3];
    right[0] = forward[1] * up[2] - forward[2] * up[1];
    right[1] = forward[2] * up[0] - forward[0] * up[2];
    right[2] = forward[0] * up[1] - forward[1] * up[0];

    simdata->Xvelocity = velocity[0] * right[0] + velocity[1] * right[1] + velocity[2] * right[2];
    simdata->Yvelocity = velocity[0] * forward[0] + velocity[1] * forward[1] + velocity[2] * forward[2];
    simdata->Zvelocity = velocity[0] * up[0] + velocity[1] * up[1] + velocity[2] * up[2];

    // SimData keeps the vertical axis in z, as BeamNG does
    simdata->worldXvelocity = velocity[0];
    simdata->worldYvelocity = velocity[1];
    simdata->worldZvelocity = velocity[2];
    simdata->worldposx = *(float*) (char*) (a + offsetof(struct beamng_motionsim, posX));
    simdata->worldposy = *(float*) (char*) (a + offsetof(struct beamng_motionsim, posY));
    simdata->worldposz = *(float*) (char*) (a + offsetof(struct beamng_motionsim, posZ));

    simdata->heading = yaw * (180.0 / M_PI);
    simdata->pitch = pitch * (180.0 / M_PI);
    simdata->roll = roll * (180.0 / M_PI);
}

void map_outgauge_outsim_data(SimData* simdata, SimMap* simmap, SimulatorEXE simexe, char* base)
{
    if(base != NULL)
    {
        char* a = base;
        int id = 0;

        // BeamNG sends MotionSim packets to the same port when it's pointed there;
        // OutGauge can't start with the magic, as BeamNG sends its time field as 0
        if (simexe == SIMULATOREXE_BEAMNG && memcmp(a, BEAMNG_MOTIONSIM_MAGIC, 4) == 0)
        {
            map_beamng_motionsim(simdata, a);
            return;
        }

        id = *(uint8_t*) (char*) (a + offsetof(struct outgauge, id));
        if(id == 2 || simexe == SIMULATOREXE_BEAMNG)
        {
            simdata->gear = *(uint8_t*) (char*) (a + offsetof(struct outgauge, gear));
            //int time = *(uint64_t*) (char*) (a + offsetof(struct outgauge, time));
            simdata->rpms = froundint( *(float*) (char*) (a + offsetof(struct outgauge, rpm)) );
            if(simdata->rpms > 0 && simdata->rpms > simdata->maxrpm)
            {
                simdata->maxrpm = simdata->rpms;
            }
            simdata->velocity = froundint( (*(float*) (char*) (a + offsetof(struct outgauge, speed))) * 3.6 );
            simdata->turboboost = *(float*) (char*) (a + offsetof(struct outgauge, turbo));
            simdata->fuel = *(float*) (char*) (a + offsetof(struct outgauge, fuel));
            simdata->gas = *(float*) (char*) (a + offsetof(struct outgauge, throttle));
            simdata->brake = *(float*) (char*) (a + offsetof(struct outgauge, brake));
            simdata->clutch = *(float*) (char*) (a + offsetof(struct outgauge, clutch));

            // OutGauge already uses simapi's gear numbering: 0 reverse, 1 neutral, 2 first
            if (simdata->gear == SIMAPI_GEAR_REVERSE)
            {
                snprintf(simdata->gearc, sizeof(simdata->gearc), "R");
            }
            else if (simdata->gear == SIMAPI_GEAR_NEUTRAL)
            {
                snprintf(simdata->gearc, sizeof(simdata->gearc), "N");
            }
            else
            {
                snprintf(simdata->gearc, sizeof(simdata->gearc), "%d", (simdata->gear - 1) % 100);
            }

            uint32_t showlights = *(uint32_t*) (char*) (a + offsetof(struct outgauge, showLights));
            simdata->handbrake = (showlights & OG_DL_HANDBRAKE) ? 1.0 : 0.0;

            // car is 4 bytes with no terminator ("beam" for BeamNG)
            uint8_t i = 0;
            simdata->car[4] = '\0';
            while(i < 4)
            {
                char b = *(char*) (char*) (a + offsetof(struct outgauge, car) + i);
                simdata->car[i] = b;
                i++;
            }

            simdata->altitude = 1;
            return;
        }
        id = 0;

        id = *(uint8_t*) (char*) (a + offsetof(struct outsim, ID));
        if(id == 1)
        {
            float pos = *(float*) (char*) (a + offsetof(struct outsim, CurrentLapDist));
            float dist = *(float*) (char*) (a + offsetof(struct outsim, IndexedDistance));
            //fprintf(stderr, "pos x: %f %f\n", pos, dist);
            for (int i = 0; i < 4; i++)
            {
                simdata->tyreslipratio[i] = *(float*) (char*) (a + offsetof(struct outsim, OSWheels) + (sizeof(OutSimWheel) * i) + offsetof(OutSimWheel, SlipRatio));
            }
            return;
        }
    }
}
