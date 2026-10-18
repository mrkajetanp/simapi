# Le Mans Ultimate

Le Mans Ultimate (LMU) is built on the rFactor 2 engine and publishes its telemetry
in a shared memory mapping named `LMU_Data`. This page describes that block's
format: the layout, the units and conventions the game actually uses, and where the
header's descriptions are wrong.

The game creates `LMU_Data` only when **Settings → Gameplay → Enable Plugins** is
on. The interface first shipped with **LMU 1.2** (December 2025). **LMU 1.3** named a
set of new fields inside the reserved bytes without moving or resizing anything
else (see [LMU 1.3 additions](#lmu-13-additions)).

## Layout

All structures use `#pragma pack(4)`: doubles are 4-byte aligned, not 8, which is
why `mDeltaTime` sits at offset 4. The embedded rF2 records declare some fields as
`long`, which is 4 bytes on Windows but 8 on 64-bit Linux, so `include/lmu.h` uses
`int32_t` for them.

### Root: `SharedMemoryObjectOut` (`struct LMUObject` in simapi)

| Offset | Size | Member | Contents |
|---:|---:|---|---|
| 0 | 332 | `generic` | event counters, `gameVersion` (i32 at 64; e.g. 14200), `FFBTorque` (f32 at 68), application info |
| 332 | 1300 | `paths` | five 260-byte path strings |
| 1632 | 126832 | `scoring` | `scoringInfo` (548) + 12 bytes + `vehScoringInfo[104]` (584 each, from absolute offset 2192) + 64 KiB scoring stream |
| 128464 | 196356 | `telemetry` | `activeVehicles` (u8), `playerVehicleIdx` (u8), `playerHasVehicle` (bool), 1 pad byte, then `telemInfo[104]` (1888 each, from absolute offset 128468) |
| | **324820** | total | |

The vehicle records are the rF2 `TelemInfoV01`, `TelemWheelV01`,
`VehicleScoringInfoV01` and `ScoringInfoV01` structures, copied byte for byte.
There are **104** vehicle slots, and the block has **no update counters**. A reader
can therefore copy a frame while the game is halfway through writing it. If that
matters, check that `mElapsedTime` has not changed during the copy.

The file a reader maps may be larger than the layout (for example rounded up to
whole pages) but must not be smaller: map `sizeof(struct LMUObject)` and no more,
since touching bytes past the end of the file raises `SIGBUS`. A block that is all
zeros has not been written by the game yet. Treat the block as written when either
`gameVersion` or `activeVehicles` is non-zero.

### Finding the player's car

```c
const LMUTelemetryData* td = &obj->telemetry;
if (td->playerHasVehicle && td->playerVehicleIdx < td->activeVehicles)
    t = &td->telemInfo[td->playerVehicleIdx];
```

`playerVehicleIdx` indexes both `telemInfo` and `vehScoringInfo`: slot `i` is the same
car in both arrays, and `vehScoringInfo[playerVehicleIdx].mIsPlayer` is set.

**`mID` is not the slot index.** Use `playerVehicleIdx` and never search by `mID`.

### Other cars in the session

The player's car is always fully populated. **Some AI cars are simulated at reduced
detail**, and for those LMU leaves these values at zero: contact-patch and ground
velocities, camber, toe, tyre pressure and temperatures, engine water and oil
temperatures, steering torque, and the driver-aid settings. Wheel rotation, loads,
forces, wear, brake temperature and suspension are still filled in.

### Vehicle telemetry (`LMUVehicleTelemetry`, 1888 bytes)

The offsets are relative to the start of one `telemInfo[]` element. Units are S397's
comments from the header unless a row says otherwise.

| Offset | Type | Field | Units / meaning |
|---:|---|---|---|
| 0 | i32 | `mID` | slot ID |
| 4 | f64 | `mDeltaTime` | s since last update |
| 12 | f64 | `mElapsedTime` | session time, s; updated at 100 Hz (`mDeltaTime` is 0.0100) |
| 20 | i32 | `mLapNumber` | current lap |
| 32 | char[64] | `mVehicleName` | |
| 96 | char[64] | `mTrackName` | |
| 184 | vec3 | `mLocalVel` | m/s, vehicle coordinates; **driving forwards gives negative `z`** (see [Coordinates](#coordinates)) |
| 208 | vec3 | `mLocalAccel` | m/s², vehicle coordinates |
| 232 | vec3[3] | `mOri` | rows of the orientation matrix |
| 304 | vec3 | `mLocalRot` | rad/s, vehicle coordinates |
| 352 | i32 | `mGear` | **-1 = reverse, 0 = neutral, 1+ = forward** |
| 356 | f64 | `mEngineRPM` | 0 when parked, so never use it to locate the block |
| 364 / 372 | f64 | `mEngineWaterTemp` / `mEngineOilTemp` | °C |
| 388 … 412 | f64 ×4 | `mUnfilteredThrottle`, `…Brake`, `…Steering`, `…Clutch` | 0..1; steering -1 (left) .. 1 (right) |
| 420 … 444 | f64 ×4 | `mFilteredThrottle`, `…Brake`, `…Steering`, `…Clutch` | filtered inputs, same ranges |
| 452 | f64 | `mSteeringShaftTorque` | torque around the steering shaft |
| 484 / 492 | f64 | `mFrontRideHeight` / `mRearRideHeight` | **always 0 in LMU**; use the per-wheel `mRideHeight`. `mDrag`, `mFrontDownforce` and `mRearDownforce` are also always 0 |
| 524 | f64 | `mFuel` | litres |
| 532 | f64 | `mEngineMaxRPM` | rev limit; safe anchor for locating the block |
| 600 | i32 | `mCurrentSector` | zero-based; sign bit set = in pit lane (`0x80000000` in the garage) |
| 604 | u8 | `mSpeedLimiter` | |
| 605 | u8 | `mMaxGears` | forward gears |
| 608 | f64 | `mFuelCapacity` | litres |
| 664 | f64 | `mRearBrakeBias` | fraction of braking on the rear |
| 672 | f64 | `mTurboBoostPressure` | **Pa, absolute**: 101325 (1 atm) on a naturally aspirated car. The header gives no unit |
| 745 … 827 | | LMU 1.3 additions | see [below](#lmu-13-additions) |
| 848 | wheel[4] | `mWheels` | **front left, front right, rear left, rear right** |

### Wheel (`LMUWheel`, 260 bytes)

The offsets are relative to the start of one `mWheels[]` element. The absolute
offset of wheel `w` of vehicle `i` is
`128468 + 1888·i + 848 + 260·w + field`.

| Offset | Type | Field | Units / meaning |
|---:|---|---|---|
| 0 | f64 | `mSuspensionDeflection` | m |
| 8 | f64 | `mRideHeight` | m |
| 16 | f64 | `mSuspForce` | pushrod load, N |
| 24 | f64 | `mBrakeTemp` | **K**, although the header says °C (see [Known discrepancies](#known-discrepancies)) |
| 32 | f64 | `mBrakePressure` | 0..1 for now (S397: "will convert to kPa in future") |
| 40 | f64 | `mRotation` | rad/s; **negative when rolling forwards** |
| 48 / 56 | f64 | `mLateralPatchVel` / `mLongitudinalPatchVel` | velocity at the contact patch, m/s; positive longitudinal on driven wheels under power. Exact meaning unknown (see below) |
| 64 / 72 | f64 | `mLateralGroundVel` / `mLongitudinalGroundVel` | ground velocity at the patch, m/s, in vehicle axes: longitudinal equals `mLocalVel.z` (forwards is negative). The header's comment repeats the patch-velocity text |
| 80 | f64 | `mCamber` | rad |
| 88 / 96 | f64 | `mLateralForce` / `mLongitudinalForce` | N |
| 104 | f64 | `mTireLoad` | N |
| 112 | f64 | `mGripFract` | approximate fraction of the patch that is sliding |
| 120 | f64 | `mPressure` | kPa |
| 128 | f64[3] | `mTemperature` | K; left / centre / right (not inside / outside) |
| 152 | f64 | `mWear` | **1.0 = new**, decreasing with wear |
| 160 | char[16] | `mTerrainName` | material prefix from the track's TDF file |
| 176 | u8 | `mSurfaceType` | 0 dry, 1 wet, 2 grass, 3 dirt, 4 gravel, 5 rumble strip, 6 special |
| 177 / 178 | bool | `mFlat` / `mDetached` | |
| 179 | u8 | `mStaticUndeflectedRadius` | **tyre radius in whole centimetres**; can change between the garage and the track |
| 180 | f64 | `mVerticalTireDeflection` | how far the tyre is compressed from its speed-dependent radius, m (the header gives no unit) |
| 188 | f64 | `mWheelYLocation` | relative to the vehicle's y |
| 196 | f64 | `mToe` | rad |
| 204 | f64 | `mTireCarcassTemperature` | K |
| 212 | f64[3] | `mTireInnerLayerTemperature` | K |
| 236 | | 1.3 additions / reserved | see below |

`mStaticUndeflectedRadius` matches the rolling radius of a free-rolling wheel to its
1 cm resolution, so slip ratio can be computed from rotation, radius and speed.

**The patch velocities are not a usable slip ratio.** Dividing
`mLongitudinalPatchVel` by `mLongitudinalGroundVel` gives the right sign but not the
slip that rotation and radius give, so simapi publishes no slip ratio for LMU. Slip
angle is `atan(lateral / longitudinal)` of the ground velocities.

**In the garage several values are zero:** tyre pressure and all tyre temperatures,
engine water and oil temperatures, and RPM. Brake temperature, wear, loads,
suspension and radius are filled in.

**Pausing is not signalled.** While the game is paused, `mInRealtime` stays 1 and
`mGamePhase` stays 5 (green flag). The block simply stops changing, with
`mElapsedTime` frozen and RPM and speed held at their last values. The only way to
detect a pause is a clock that stops advancing.

### LMU 1.3 additions

These fields occupy bytes that `include/lmu.h` still declares as `mExpansion`. They
fit those bytes exactly, so every older offset is unchanged.

Vehicle telemetry (replacing `mExpansion[103]` at 745; `mExpansion[20]` remains at 828):

| Offset | Type | Field | Meaning |
|---:|---|---|---|
| 745 | bool | `mLapInvalidated` | |
| 746 | bool | `mABSActive` | **ABS intervening right now**; toggles as the ABS cycles. Mapped to `abs` |
| 747 | bool | `mTCActive` | traction control intervening right now |
| 748 | bool | `mSpeedLimiterActive` | |
| 749 | u8 | `mWiperState` | 0 off, 1 auto, 2 slow, 3 fast |
| 750 / 751 | u8 | `mTC` / `mTCMax` | TC level / steps |
| 752 / 753 | u8 | `mTCSlip` / `mTCSlipMax` | |
| 754 / 755 | u8 | `mTCCut` / `mTCCutMax` | |
| 756 / 757 | u8 | `mABS` / `mABSMax` | ABS level / steps |
| 758 / 759 | u8 | `mMotorMap` / `mMotorMapMax` | |
| 760 / 761 | u8 | `mMigration` / `mMigrationMax` | brake migration |
| 762 … 765 | u8 | `mFrontAntiSway`, `…Max`, `mRearAntiSway`, `…Max` | |
| 766 | u8 | `mLiftAndCoastProgress` | 0..255 |
| 767 | u8 | `mTrackLimitsSteps` | |
| 768 | f32 | `mRegen` | kW |
| 772 | f32 | `mStateOfCharge` | % |
| 776 | f32 | `mVirtualEnergy` | fraction |
| 780 … 792 | f32 ×4 | `mTimeGapCarAhead`, `…CarBehind`, `…PlaceAhead`, `…PlaceBehind` | s |
| 796 | char[30] | `mVehicleModel` | brand and model |
| 826 / 827 | u8 | `mVehicleClass` / `mVehicleChampionship` | enums (Hypercar, LMP2, … / championship and year) |

Wheel (replacing `mExpansion[24]` at 236; `mExpansion[18]` remains at 242):
`mOptimalTemp` f32 at 236 (°C), `mCompoundIndex` u8 at 240, `mCompoundType` u8 at
241 (0 soft, 1 medium, 2 hard, 3 wet).

Scoring info (replacing `mExpansion[200]` at 340; `mExpansion[187]` remains at 353):
`mSessionTimeRemaining` f32 at 340, `mTimeOfDay` f32 at 344, `mIsFixedSetup` 348,
`mTrackGripLevel` 349, `mCloudCoverage` 350, `mTrackLimitsStepsPerPenalty` 351,
`mTrackLimitsStepsPerPoint` 352.

`include/lmu.h` also names five fields inside the 48 reserved bytes of the rF2
vehicle scoring record: `mSteamID` u64 at 536, `mVehFilename` char[32] at 544,
`mAttackMode` u16 at 576, `mFuelFraction` u8 at 578 and `mDRSState` bool at 579.

### Coordinates

LMU uses rFactor 2's coordinate systems. The world coordinate system is left-handed
with +y up. In vehicle coordinates:

- **+x points out of the left side** of the car (from the driver's point of view)
- **+y points out of the roof**
- **+z points out of the back**, so **driving forwards gives a negative `mLocalVel.z`**

Rotations: +x pitches up, +y yaws to the right, +z rolls to the right. Compared with
ISO vehicle coordinates (+x forward, +y right, +z up), a -z velocity in rF2 is a +x
velocity in ISO, and accelerating forwards gives a negative `mLocalAccel.z`.

## Known discrepancies

| Field | What the header says | Actual |
|---|---|---|
| `mBrakeTemp` | "Celsius" | **Kelvin** |
| `mWear` | "0.0-1.0, fraction of maximum" | **1.0 = new**, decreasing with wear |
| `mTurboBoostPressure` | no unit | **Pa, absolute** |
| `mRotation` | rad/s, no sign | **negative forwards**; negate it for forward-positive wheel speed |
| `mID` | slot ID | **not the slot index**: see [Finding the player's car](#finding-the-players-car) |
| `mFrontRideHeight`, `mRearRideHeight`, `mDrag`, `mFrontDownforce`, `mRearDownforce` | documented | **always 0** |

## Sources

| Source | Revision | Used for |
|---|---|---|
| [TinyPedal/pyLMUSharedMemory](https://github.com/TinyPedal/pyLMUSharedMemory) `lmu_data.py`, `lmu_mmap.py` | `6a8dd831` (2026-10-14) | full 1.3 layout; S397 field names, types and comments (derived from S397's `SharedMemoryInterface.hpp`, which is not redistributed) |
| [Swizzjack/lmu-pitwall](https://github.com/Swizzjack/lmu-pitwall) `bridge/src/shared_memory/`, `tools/lmu-probe/` | `a171b80a` (2026-08-03) | size assertions, the Enable Plugins requirement, field names derived from S397's header |
| [TinyPedal/TinyPedal](https://github.com/TinyPedal/TinyPedal) `tinypedal/adapter/lmu_reader.py` | `ad8e84e5` (2026-10-17) | unit conversions in practice |
| [TheIronWolfModding/rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin) `Include/InternalsPlugin.hpp` | `48aa12db` (2024-12-31) | S397 rF2 base structs, coordinate system |
| [fpauker/lmu-rpm-leds](https://github.com/fpauker/lmu-rpm-leds) README | `e0802f16` (2026-10-02) | offsets, player index |
| [LMU V1.2 changelog](https://www.bsimracing.com/le-mans-ultimate-update-v1-2-full-changelog/) | | version history |
