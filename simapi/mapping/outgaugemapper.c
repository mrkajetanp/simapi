#include <stddef.h>
#include <math.h>
#include <stdio.h>

#include "../simmap.h"
#include "../simdata.h"
#include "../simapi.h"
#include "../simmapper.h"
#include "../outgauge.h"

#include "../../include/outgauge.h"


static int froundint(float f)
{
    return trunc(nearbyint(f));
}

void map_outgauge_outsim_data(SimData* simdata, SimMap* simmap, SimulatorEXE simexe, char* base)
{
    if(base != NULL)
    {
        char* a = base;
        int id = 0;

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
