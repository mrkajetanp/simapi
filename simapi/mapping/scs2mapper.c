#include <stddef.h>
#include <stdio.h>
#include <math.h>

#include "../simmap.h"
#include "../simdata.h"
#include "../simapi.h"
#include "../simmapper.h"
#include "../scs2.h"

#include "../../include/scs2data.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SCS2_WHEELS 16
// wheels this close to each other along the truck (metres) are on the same axle
#define SCS2_AXLE_TOLERANCE 0.25

static int droundint(double d)
{
    return trunc(nearbyint(d));
}

/*
 * Choose which of the truck's wheels to report as front left, front right,
 * rear left and rear right. The SCS SDK doesn't define a wheel order, so use
 * the configured wheel positions (vehicle space: X right, Z backwards). The
 * front axle is the one furthest forward. The rear axle is the first powered
 * axle behind it, or the next axle back if none is powered. Only wheels the
 * game physically simulates are considered. Until the truck configuration
 * has arrived, fall back to wheels 0..3.
 */
static void scs2_pick_wheels(char* a, int wheels[4])
{
    for (int i = 0; i < 4; i++)
    {
        wheels[i] = i;
    }

    uint32_t count = *(uint32_t*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_ui.truckWheelCount));
    if (count > SCS2_WHEELS)
    {
        count = SCS2_WHEELS;
    }

    float x[SCS2_WHEELS];
    float z[SCS2_WHEELS];
    bool simulated[SCS2_WHEELS];
    bool powered[SCS2_WHEELS];
    bool anysimulated = false;
    for (uint32_t i = 0; i < count; i++)
    {
        x[i] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_fv.truckWheelPositionX) + (sizeof(float) * i));
        z[i] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_fv.truckWheelPositionZ) + (sizeof(float) * i));
        simulated[i] = *(bool*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_b.truckWheelSimulated) + (sizeof(bool) * i));
        powered[i] = *(bool*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_b.truckWheelPowered) + (sizeof(bool) * i));
        anysimulated = anysimulated || simulated[i];
    }
    for (uint32_t i = 0; i < count; i++)
    {
        if (anysimulated == false)
        {
            simulated[i] = true;
        }
    }

    int front = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        if (simulated[i] && (front < 0 || z[i] < z[front]))
        {
            front = i;
        }
    }
    if (front < 0)
    {
        return;
    }

    int rear = -1;
    for (uint32_t i = 0; i < count; i++)
    {
        if (simulated[i] == false || z[i] <= z[front] + SCS2_AXLE_TOLERANCE)
        {
            continue;
        }
        if (rear < 0 || (powered[i] && !powered[rear]) || (powered[i] == powered[rear] && z[i] < z[rear]))
        {
            rear = i;
        }
    }
    if (rear < 0)
    {
        return;
    }

    // outermost wheel on each side of each axle
    int picked[4] = { -1, -1, -1, -1 };
    for (uint32_t i = 0; i < count; i++)
    {
        if (simulated[i] == false)
        {
            continue;
        }
        int axle;
        if (fabs(z[i] - z[front]) <= SCS2_AXLE_TOLERANCE)
        {
            axle = 0;
        }
        else if (fabs(z[i] - z[rear]) <= SCS2_AXLE_TOLERANCE)
        {
            axle = 2;
        }
        else
        {
            continue;
        }
        int side = axle + (x[i] < 0 ? 0 : 1);
        int p = picked[side];
        if (p < 0 || (side % 2 == 0 && x[i] < x[p]) || (side % 2 == 1 && x[i] > x[p]))
        {
            picked[side] = i;
        }
    }

    if (picked[0] >= 0 && picked[1] >= 0 && picked[2] >= 0 && picked[3] >= 0)
    {
        for (int i = 0; i < 4; i++)
        {
            wheels[i] = picked[i];
        }
    }
}

void map_trucks_data(SimData* simdata, SimMap* simmap)
{


    char* a;
    char* b;
    char* c;
    char* d;

    a = simmap->scs2.telemetry_map_addr;

    // the plugin clears sdkActive when the game shuts the SDK down, and keeps
    // paused set while the game is paused or not yet driving
    bool active = *(bool*) (char*) (a + offsetof(struct scs2TelemetryMap_s, sdkActive));
    bool paused = *(bool*) (char*) (a + offsetof(struct scs2TelemetryMap_s, paused));
    if (active == false)
    {
        simdata->simstatus = SIMAPI_STATUS_OFF;
    }
    else if (paused == true)
    {
        simdata->simstatus = SIMAPI_STATUS_MENU;
    }
    else
    {
        simdata->simstatus = SIMAPI_STATUS_ACTIVEPLAY;
    }

    // "Brand Name"; the plugin's strings are 64 bytes and may lack a terminator
    char* brand = (char*) (a + offsetof(struct scs2TelemetryMap_s, config_s.truckBrand));
    char* name = (char*) (a + offsetof(struct scs2TelemetryMap_s, config_s.truckName));
    if (brand[0] != '\0' && name[0] != '\0')
    {
        snprintf(simdata->car, sizeof(simdata->car), "%.64s %.64s", brand, name);
    }
    else if (name[0] != '\0')
    {
        snprintf(simdata->car, sizeof(simdata->car), "%.64s", name);
    }
    else
    {
        snprintf(simdata->car, sizeof(simdata->car), "default");
    }


    // speed is negative in reverse (SCS SDK truck.speed); simdata->velocity is unsigned
    simdata->velocity = droundint(3.6 * fabs(*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.speed))));
    simdata->rpms = droundint(*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.engineRpm)));
    simdata->brake = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userBrake));
    simdata->gas = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userThrottle));
    // pedal input 0..1, 1 = fully pressed (SCS SDK input_clutch), not the effective clutch
    simdata->clutch = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userClutch));
    simdata->maxrpm = droundint( *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_f.engineRpmMax)));
    simdata->maxgears = *(uint32_t*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_ui.gears));

    // SCS steering input is counterclockwise (left) positive; SimData uses right positive like the other sims
    simdata->steer = -*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.userSteer));
    simdata->handbrake = *(bool*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_b.parkBrake)) ? 1.0 : 0.0;

    // litres
    simdata->fuel = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.fuel));
    simdata->fuelcapacity = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_f.fuelCapacity));

    // SCS gives one brake temperature for the whole truck
    double braketemp = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.brakeTemperature));
    for (int i = 0; i < 4; i++)
    {
        simdata->braketemp[i] = braketemp;
    }

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

    // wheels: SCS reports angular velocity in rotations per second, SimData uses rad/s
    int wheels[4];
    scs2_pick_wheels(a, wheels);
    double new_suspension[4];
    for (int i = 0; i < 4; i++)
    {
        int w = wheels[i];
        simdata->tyreRPS[i] = 2.0 * M_PI * *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelVelocity) + (sizeof(float) * w));
        simdata->tyrediameter[i] = 2.0 * *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, config_f.truckWheelRadius) + (sizeof(float) * w));
        new_suspension[i] = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_f.truck_wheelSuspDeflection) + (sizeof(float) * w));
    }
    map_suspension_velocity(simdata, new_suspension);
    for (int i = 0; i < 4; i++)
    {
        simdata->suspension[i] = new_suspension[i];
    }

    // local velocity; SCS vehicle space is X right, Y up, Z backwards, while
    // SimData has Yvelocity forward and Zvelocity vertical
    simdata->Xvelocity = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_fv.lv_accelerationX));
    simdata->Yvelocity = -*(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_fv.lv_accelerationZ));
    simdata->Zvelocity = *(float*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_fv.lv_accelerationY));

    // orientation in degrees; SCS stores fractions of a turn, heading counterclockwise from north
    double heading = 360.0 * *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.rotationX));
    if (heading > 180.0)
    {
        heading -= 360.0;
    }
    simdata->heading = heading;
    simdata->pitch = 360.0 * *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.rotationY));
    simdata->roll = 360.0 * *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.rotationZ));

    // world position; SCS world space is X east, Y up, Z south. SimData keeps
    // the vertical axis in worldposz, as the AC mapper does
    simdata->worldposx = *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.coordinateX));
    simdata->worldposy = *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.coordinateZ));
    simdata->worldposz = *(double*) (char*) (a + offsetof(struct scs2TelemetryMap_s, truck_dp.coordinateY));

    simdata->altitude = 1;

}
