#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "../simmap.h"
#include "../simdata.h"
#include "../simapi.h"
#include "../simmapper.h"
#include "../lmudef.h"

#include "../../include/lmu.h"

#define KELVIN_TO_CELSIUS 273.15
#define ATMOSPHERE_PA 101325.0

// longer than this between two frames is a pause or a reload, not motion
#define LMU_MAX_SUSPENSION_DT 0.1
// a paused game stops producing frames, and its last velocity must not
// keep driving effects
#define LMU_STALE_FRAME_MS 50

// at a standstill both ground velocities are noise around zero and their ratio is meaningless
#define LMU_MIN_SLIP_SPEED 1.0

// LMU 1.3 placed mABSActive in the second byte of what include/lmu.h still
// declares as the telemetry's mExpansion
#define LMU_EXPANSION_ABS_ACTIVE 1

static int droundint(double d)
{
    return trunc(nearbyint(d));
}

static LapTime lmu_convert_to_simdata_laptime(double lmu_laptime)
{
    if(lmu_laptime <= 0)
    {
        return (LapTime)
        {
            0, 0, 0, 0
        };
    }
    uint64_t ms = lmu_laptime * 1000;
    LapTime l;
    l.hours = ms / 3600000;
    l.minutes = ms / 60000 % 60;
    l.seconds = ms / 1000 % 60;
    l.fraction = ms % 1000;
    return l;
}

// LMU fills temperatures with 0 where it does not simulate them (garage,
// reduced-detail AI), which must not turn into -273.15
static double lmu_kelvin_to_celsius(double k)
{
    if(k <= 0)
    {
        return 0;
    }
    return k - KELVIN_TO_CELSIUS;
}

static void lmu_copy_string(char* dest, const char* src, size_t srclen)
{
    size_t len = strnlen(src, srclen);
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// rF2 lineage numbers sectors 0 = sector 3, 1 = sector 1, 2 = sector 2
static int lmu_sector_to_simdata_sector(int sector)
{
    if(sector < 0 || sector > 2)
    {
        return 0;
    }
    return (sector + 2) % 3;
}

static int lmu_phase_to_simdata_flag(int phase)
{
    if(phase < 5 || phase > 8)
    {
        return 0;
    }
    return phase - 5;
}

static int lmu_session_to_simdata_session(int session)
{
    // 0 test day, 1-4 practice, 5-8 qualifying, 9 warmup, 10-13 race
    if(session >= 5 && session <= 8)
    {
        return 1;
    }
    if(session >= 10 && session <= 13)
    {
        return 2;
    }
    return 0;
}

// map_suspension_velocity() divides by wall-clock time between mapper runs,
// but LMU writes at 100 Hz and simd samples every 16 ms, so each sample would
// cover one or two frames over the same 16 ms; the game's own clock does not.
// Returns whether the game has stopped producing frames
static bool lmu_map_suspension(SimData* simdata, LMUMap* map, const LMUVehicleTelemetry* t)
{
    double dt = t->mElapsedTime - map->prev_elapsed_time;
    if(dt == 0 && map->prev_elapsed_time >= 0)
    {
        // same frame as last time; keep the velocity it produced
        if(simdata->mtick - map->prev_frame_mtick > LMU_STALE_FRAME_MS)
        {
            for(int i = 0; i < 4; i++)
            {
                simdata->suspvelocity[i] = 0;
            }
            return true;
        }
        return false;
    }
    for(int i = 0; i < 4; i++)
    {
        double deflection = t->mWheels[i].mSuspensionDeflection;
        simdata->suspvelocity[i] = 0;
        if(map->prev_elapsed_time >= 0 && dt > 0 && dt <= LMU_MAX_SUSPENSION_DT)
        {
            simdata->suspvelocity[i] = fabs(deflection - map->prev_deflection[i]) / dt;
        }
        simdata->suspension[i] = deflection;
        map->prev_deflection[i] = deflection;
    }
    map->prev_elapsed_time = t->mElapsedTime;
    map->prev_frame_mtick = simdata->mtick;
    return false;
}

static void lmu_set_car_pit_state(CarData* car, const LMUVehicleScoring* s)
{
    car->inpit = false;
    car->inpitlane = false;
    car->ingarage = false;
    car->inpitstopped = false;
    car->inpitentrance = false;
    car->inpitexit = false;
    if(s->mPitState == 2)
    {
        car->inpitentrance = true;
    }
    if(s->mPitState == 3)
    {
        car->inpitstopped = true;
    }
    if(s->mPitState == 4)
    {
        car->inpitexit = true;
    }
    if(s->mPitState >= 2)
    {
        car->inpit = true;
    }
    if(s->mPitState == 2 || s->mPitState == 4)
    {
        car->inpitlane = true;
    }
    if(s->mInGarageStall)
    {
        car->ingarage = true;
        car->inpit = true;
    }
}

void map_lmu_data(SimData* simdata, SimMap* simmap)
{
    const struct LMUObject* lmu = simmap->lmu.telemetry_map_addr;
    const LMUScoringInfo* si = &lmu->scoring.scoringInfo;
    const LMUTelemetryData* td = &lmu->telemetry;

    // playerVehicleIdx indexes both arrays directly; mID is not the slot index
    // activeVehicles is whatever the file holds, which need not respect the
    // 104 slots the arrays actually have
    if(!td->playerHasVehicle || td->playerVehicleIdx >= td->activeVehicles || td->playerVehicleIdx >= LMU_MAX_MAPPED_VEHICLES)
    {
        simdata->simstatus = SIMAPI_STATUS_MENU;
        return;
    }
    int veh = td->playerVehicleIdx;
    const LMUVehicleTelemetry* t = &td->telemInfo[veh];
    const LMUVehicleScoring* s = &lmu->scoring.vehScoringInfo[veh];

    simdata->simstatus = si->mInRealtime ? SIMAPI_STATUS_ACTIVEPLAY : SIMAPI_STATUS_MENU;

    simdata->velocity = abs(droundint(3.6 * t->mLocalVel.z));
    simdata->rpms = t->mEngineRPM > 0 ? droundint(t->mEngineRPM) : 0;
    simdata->maxrpm = droundint(t->mEngineMaxRPM);
    simdata->maxgears = t->mMaxGears;
    simdata->gas = t->mUnfilteredThrottle;
    simdata->brake = t->mUnfilteredBrake;
    simdata->clutch = t->mUnfilteredClutch;
    simdata->steer = t->mUnfilteredSteering;
    simdata->fuel = t->mFuel;
    simdata->fuelcapacity = t->mFuelCapacity;
    simdata->brakebias = t->mRearBrakeBias;
    simdata->turboboost = 0;
    if(t->mTurboBoostPressure > 0)
    {
        simdata->turboboost = (t->mTurboBoostPressure - ATMOSPHERE_PA) / 100000.0;
    }
    simdata->abs = 0;
    if(t->mExpansion[LMU_EXPANSION_ABS_ACTIVE] != 0)
    {
        simdata->abs = 1;
    }
    simdata->handbrake = 0;
    simdata->altitude = 1;

    int gear = t->mGear;
    simdata->gearc[0] = gear + 48;
    if (gear < 0)
    {
        simdata->gearc[0] = 'R';
    }
    if (gear == 0)
    {
        simdata->gearc[0] = 'N';
    }
    simdata->gearc[1] = 0;
    simdata->gear = gear + 1;

    // LMU vehicle axes are +x left, +y up, +z back; simapi wants forward positive
    simdata->Xvelocity = -t->mLocalVel.x;
    simdata->Yvelocity = -t->mLocalVel.z;
    simdata->Zvelocity = -t->mLocalVel.y;

    const LMUVect3* ori = t->mOri;
    simdata->worldXvelocity = ori[0].x * t->mLocalVel.x + ori[0].y * t->mLocalVel.y + ori[0].z * t->mLocalVel.z;
    simdata->worldYvelocity = ori[2].x * t->mLocalVel.x + ori[2].y * t->mLocalVel.y + ori[2].z * t->mLocalVel.z;
    simdata->worldZvelocity = ori[1].x * t->mLocalVel.x + ori[1].y * t->mLocalVel.y + ori[1].z * t->mLocalVel.z;

    for(int i = 0; i < 4; i++)
    {
        const LMUWheel* w = &t->mWheels[i];

        // mRotation is negative when rolling forwards
        simdata->tyreRPS[i] = -w->mRotation;

        // the static radius matched speed / rotation on free-rolling wheels,
        // so no deflection correction is applied
        if(w->mStaticUndeflectedRadius > 0)
        {
            simdata->tyrediameter[i] = 2 * w->mStaticUndeflectedRadius / 100.0;
        }

        // a slip ratio from the contact patch velocity disagreed with wheel
        // speed by several percent on the same frame, so none is published
        // and consumers derive slip from tyreRPS and the diameter instead;
        // any non-zero value here would override that
        simdata->tyreslipratio[i] = 0;
        simdata->tyreslipangle[i] = 0;
        if(fabs(w->mLongitudinalGroundVel) > LMU_MIN_SLIP_SPEED)
        {
            simdata->tyreslipangle[i] = atan(w->mLateralGroundVel / w->mLongitudinalGroundVel);
        }

        simdata->tyrewear[i] = w->mWear;
        simdata->tyretemp[i] = lmu_kelvin_to_celsius(w->mTireCarcassTemperature);
        simdata->braketemp[i] = lmu_kelvin_to_celsius(w->mBrakeTemp);
        simdata->tyrepressure[i] = w->mPressure;

    }

    // a paused LMU keeps mInRealtime and its game phase and just stops its
    // clock, so its last rpm and speed would keep every effect running
    if(lmu_map_suspension(simdata, &simmap->lmu, t))
    {
        simdata->rpms = 0;
        simdata->velocity = 0;
        simdata->Xvelocity = 0;
        simdata->Yvelocity = 0;
        simdata->Zvelocity = 0;
        simdata->worldXvelocity = 0;
        simdata->worldYvelocity = 0;
        simdata->worldZvelocity = 0;
        simdata->abs = 0;
        for(int i = 0; i < 4; i++)
        {
            simdata->tyreRPS[i] = 0;
        }
    }

    simdata->airtemp = si->mAmbientTemp;
    simdata->tracktemp = si->mTrackTemp;
    simdata->session = lmu_session_to_simdata_session(si->mSession);

    double trackdist = si->mLapDist;
    double pos = s->mLapDist;
    if(pos < 0)
    {
        pos = (-1 * pos) + .5;
    }
    if(trackdist > 0)
    {
        simdata->tracksamples = ceil(trackdist * 4);
        simdata->playerspline = pos / trackdist;
    }

    simdata->lap = t->mLapNumber + 1;
    simdata->position = s->mPlace;
    simdata->lastlap = lmu_convert_to_simdata_laptime(s->mLastLapTime);
    simdata->bestlap = lmu_convert_to_simdata_laptime(s->mBestLapTime);
    simdata->currentlap = lmu_convert_to_simdata_laptime(s->mTimeIntoLap);
    simdata->sessiontime = lmu_convert_to_simdata_laptime(t->mElapsedTime);

    simdata->numlaps = si->mMaxLaps;
    if(si->mMaxLaps == INT_MAX)
    {
        simdata->numlaps = 0;
    }
    simdata->sectorindex = lmu_sector_to_simdata_sector(s->mSector);
    simdata->playerflag = s->mFlag == 6 ? SIMAPI_FLAG_BLUE : SIMAPI_FLAG_GREEN;
    simdata->courseflag = lmu_phase_to_simdata_flag(si->mGamePhase);

    lmu_copy_string(simdata->car, t->mVehicleName, sizeof(t->mVehicleName));
    lmu_copy_string(simdata->track, t->mTrackName, sizeof(t->mTrackName));
    lmu_copy_string(simdata->driver, s->mDriverName, sizeof(s->mDriverName));
    lmu_copy_string(simdata->tyrecompound, t->mRearTireCompoundName, sizeof(t->mRearTireCompoundName));

    // world axes follow the same left-handed, y-up layout as the vehicle axes
    simdata->worldposx = t->mPos.x;
    simdata->worldposz = t->mPos.y;
    simdata->worldposy = t->mPos.z;

    int numcars = td->activeVehicles;
    if (numcars > LMU_MAX_MAPPED_VEHICLES)
    {
        numcars = LMU_MAX_MAPPED_VEHICLES;
    }
    simdata->numcars = numcars;
    // simapi_set_proximity_data() treats cars[0] as the player, while LMU puts
    // the player in any slot, so the player's slot trades places with slot 0
    for(int i = 0; i < numcars; i++)
    {
        int slot = i;
        if(i == 0)
        {
            slot = veh;
        }
        else if(i == veh)
        {
            slot = 0;
        }
        const LMUVehicleTelemetry* ct = &td->telemInfo[slot];
        const LMUVehicleScoring* cs = &lmu->scoring.vehScoringInfo[slot];
        CarData* car = &simdata->cars[i];

        car->lap = ct->mLapNumber;
        car->pos = cs->mPlace;
        lmu_set_car_pit_state(car, cs);
        car->lastlap = lmu_convert_to_simdata_laptime(cs->mLastLapTime);
        car->bestlap = lmu_convert_to_simdata_laptime(cs->mBestLapTime);
        lmu_copy_string(car->driver, cs->mDriverName, sizeof(cs->mDriverName));
        lmu_copy_string(car->car, cs->mVehicleName, sizeof(cs->mVehicleName));
        car->xpos = ct->mPos.x;
        car->zpos = ct->mPos.y;
        car->ypos = ct->mPos.z;
    }

    simapi_set_proximity_data(simdata, numcars, 1);
}
