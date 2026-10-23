#include <stddef.h>
#include <stdio.h>
#include <math.h>

#include "../simmap.h"
#include "../simdata.h"
#include "../simapi.h"
#include "../simmapper.h"
#include "../scs2.h"

#include "../../include/scs2data.h"

static int droundint(double d)
{
    return trunc(nearbyint(d));
}

void map_trucks_data(SimData* simdata, SimMap* simmap)
{


    char* a;
    char* b;
    char* c;
    char* d;

    a = simmap->scs2.telemetry_map_addr;

    simdata->simstatus = 2;
    simdata->car[0] ='d';
    simdata->car[1]='e';
    simdata->car[2]='f';
    simdata->car[3]='a';
    simdata->car[4]='u';
    simdata->car[5]='l';
    simdata->car[6]='t';
    simdata->car[7]='\0';


    // speed is negative in reverse (SCS SDK truck.speed); simdata->velocity is unsigned
    simdata->velocity = droundint(3.6 * fabs(*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.speed))));
    simdata->rpms = droundint(*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.engineRpm)));
    simdata->brake = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userBrake));
    simdata->gas = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userThrottle));
    simdata->tyreRPS[0] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelVelocity));
    simdata->tyreRPS[1] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelVelocity) + (sizeof(float) * 1));
    simdata->tyreRPS[2] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelVelocity) + (sizeof(float) * 2));
    simdata->tyreRPS[3] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelVelocity) + (sizeof(float) * 3));
    simdata->maxrpm = droundint( *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_f.engineRpmMax)));

    // SCS gears: >0 forward, 0 neutral, <0 reverse (any of several reverse gears)
    int32_t gear = *(int32_t*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_i.gear));
    if (gear < 0)
    {
        simdata->gear = SIMAPI_GEAR_REVERSE;
    }
    else
    {
        simdata->gear = gear + 1;
    }

    // the gear text follows the dashboard, which can differ from the engine gear
    int32_t dashgear = *(int32_t*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_i.gearDashboard));
    uint32_t reversegears = *(uint32_t*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_ui.gears_reverse));
    if (dashgear > 0 && dashgear < 100)
    {
        snprintf(simdata->gearc, sizeof(simdata->gearc), "%d", dashgear);
    }
    else if (dashgear < 0 && dashgear > -10 && reversegears > 1)
    {
        snprintf(simdata->gearc, sizeof(simdata->gearc), "R%d", -dashgear);
    }
    else if (dashgear < 0)
    {
        snprintf(simdata->gearc, sizeof(simdata->gearc), "R");
    }
    else
    {
        snprintf(simdata->gearc, sizeof(simdata->gearc), "N");
    }

    simdata->altitude = 1;

}
