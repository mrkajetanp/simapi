# Le Mans Ultimate

Le Mans Ultimate (LMU) is built on the rFactor 2 engine and can expose telemetry in
two ways:

| Interface | Name | Needs | simapi status |
|---|---|---|---|
| **Built-in shared memory** (LMU 1.2+) | `LMU_Data` | *Enable Plugins* turned on in game; on Linux a bridge (see below) | supported; preferred when the game has written `/dev/shm/LMU_Data`, otherwise falls back to the plugin after 30 s |
| rFactor 2 Shared Memory Map Plugin | `$rFactor2SMMP_*$` | third-party DLL ([Wine fork](https://github.com/schlegp/rF2SharedMemoryMapPlugin_Wine)) | supported, see [rFactor 2](/simapi/rfactor2) |

This page describes the built-in `LMU_Data` interface. Everything here was checked
on 2026-10-18 against the sources listed under [Sources](#sources) and against four
dumps of `LMU_Data` from a running game reporting `gameVersion` 14200 (see
[Verification status](#verification-status)).
Where the sources disagree, or where something has not been seen in real data, this
page says so.

## Enabling it in the game

`LMU_Data` only exists when **Settings → Gameplay → Enable Plugins** is on, and LMU
has to be fully restarted after changing it. With the option off, the game
publishes no `LMU_Data` mapping at all. The built-in interface needs no plugin DLL.

The shared memory header first shipped with **LMU 1.2** (December 2025), in the game
folder at `Support/SharedMemoryInterface/SharedMemoryInterface.hpp`. **LMU 1.3**
(by March 2026) named a set of new fields inside the reserved bytes, without
moving or resizing anything else (see [LMU 1.3 additions](#lmu-13-additions)).
LMU 1.4.2 (September 2026) lists "Fixed some threading issues with shared memory" in its patch notes.

## Getting `LMU_Data` on Linux

LMU runs under Proton, and Proton does not expose Windows named mappings to Linux
processes under a name they can open (see [Shared Memory](/simapi/sharedmemory)).
There are three ways around that. simapi reads `/dev/shm/LMU_Data`, which the first
two provide. Use only one of them at a time, because each of them owns that file.

### simd with simshmbridge (recommended)

[simd](/simapi/simd) can do everything by itself, together with the `lmubridge.exe`
helper from [simshmbridge](https://github.com/spacefreak18/simshmbridge):

1. Build the helper: `make -f Makefile.lmu` in simshmbridge produces
   `assets/lmubridge.exe`.
2. Make sure `~/.config/simd/simd.config` has the `LeMansUltimate` entry from
   [simd.config](https://github.com/Spacefreak18/simapi/blob/master/simd/conf/simd.config).
   simd only launches a bridge for games listed there.
3. Set LMU's Steam launch options to
   ```
   SIMD_BRIDGE_EXE=/path/to/simshmbridge/assets/lmubridge.exe %command%
   ```
4. Turn on **Enable Plugins** in the game (see above).
5. Start simd before the game, with its automatic memory mapping and bridging left
   on (the default; `-h` and `-a` turn them off).

simd creates `/dev/shm/LMU_Data` (`LMU_SIZE` bytes) when it starts. When it sees
`Le Mans Ultimate.exe`, it reads `SIMD_BRIDGE_EXE`, the Proton path and the prefix
from the game's environment and starts `lmubridge.exe` inside the game's Proton
session. The bridge registers simd's file as the Windows named mapping `LMU_Data`.
When the game then creates its mapping, it gets that one, so it **writes straight
into the Linux file** and nothing is copied. simd starts mapping as soon as the
block contains data, and stops the bridge when the game exits.

**The bridge must be running before the game creates its mapping.** If the game
gets there first, it gets an ordinary memory-backed mapping. The bridge then opens
that instead, and the Linux file stays zero. simd then logs at INFO level that
Le Mans Ultimate is running but `/dev/shm/LMU_Data` is still empty, thirty seconds
after it first saw the game. Only then does it try the rFactor 2 plugin. Waiting
first keeps a plugin found while the game is still loading from holding the sim
for the whole session. In a test with a stand-in game, simd had the bridge running
3 to 5 seconds after the game process appeared. simshmbridge's own instructions
start the bridge 5 seconds after the game, so LMU appears to create its mapping
later than that. With LMU 1.4.3 under GE-Proton10-34-LMU, the bridge started by
simd won the race: the game's view of `LMU_Data` was simd's file.

**The file must be at least as large as the game's mapping.** The layout below is
324,820 bytes. simapi's `LMU_SIZE` was originally 250,000, which is too small; it has
been 505,000 since commit `5a03496` ("Fixed lmu by changing file size", #22). The
simshmbridge author notes that LMU crashes after the splash screen if the bridge does
not allocate enough space.

simd zeroes the file whenever it stops mapping a game, including when the player
returns to the garage, and LMU keeps writing to it afterwards. simapi therefore
accepts the block as live when either `gameVersion` or `activeVehicles` is set.

### simshmbridge by hand

Without simd's automatic bridging, build `lmushm` as well (`make -f Makefile.lmu`).
Start `lmushm` first, then the game together with the bridge, e.g. as a Steam
launch option:

```
%command% & sleep 5 && ~/path/to/proton run ~/simshmbridge/assets/lmubridge.exe
```

### winecarte

[winecarte](https://github.com/srounce/winecarte) supports LMU out of the box
(Steam launch option `winecarte-run %command%`). Its `wine2linux.exe` runs inside the
game's Proton session and **copies** the game's `LMU_Data` mapping into
`/dev/shm/LMU_Data`. Each copy is triggered by the game's named event `LMU_Data_Event`,
which LMU signals on every update; winecarte falls back to polling every 16 ms if
the event is missing. Because it copies, the start-up order does not matter.

The Linux file is the size of the mapped region rounded up to whole pages:
**327,680 bytes**, not simapi's `LMU_SIZE` of 505,000. A reader must not touch bytes
past the end of the file, because doing so raises `SIGBUS`.

winecarte also knows about LMU's lock objects, `LMU_SharedMemoryLockData` (two
`int32`s, `waiters` and `busy`) and `LMU_SharedMemoryLockEvent`. It does not use them
for LMU by default, so a copy can contain a frame the game was halfway through
writing.

**Do not combine winecarte with simd's automatic memory mapping.** simd creates
`/dev/shm/LMU_Data` itself, resizes it to `LMU_SIZE`, zeroes it whenever it stops
mapping, and deletes it when it exits, so it would fight winecarte over the file.
To use winecarte, run simd with `-h`, which turns the mapping off for every game.

### Things to be aware of

- **The file existing does not mean the game is running.** simd and `lmushm`
  create `/dev/shm/LMU_Data` before the game starts, and it stays all zeros until
  the game writes to it.
- **pyLMUSharedMemory (used by TinyPedal) resizes the file.** Since its commit
  `ec902acb` (2026-10-13), on Linux it truncates `/dev/shm/LMU_Data` and fills it
  with zeros whenever the file's size is not exactly 324,820 bytes. That size
  differs from simapi's 505,000, so running TinyPedal alongside a simapi-created
  file may wipe it. This is read from its source and has not been tested.

### memfd via `/proc` (alternative, not used by simapi)

Without a bridge, lmu-rpm-leds reports that Wine backs the game's named mapping
with a `memfd` object. It has no path in `/dev/shm`, but the owning user can read it
through `/proc/<pid>/fd/<n>` of `Le Mans Ultimate.exe` or `PluginsAdapter.exe`, the
separate process LMU runs plugins in.
[lmu-rpm-leds](https://github.com/fpauker/lmu-rpm-leds) reads LMU this way, with no
bridge. Its README notes that several blocks that look valid can exist at the same
time, one of them a stale copy that barely updates. If you have to find the block
by its contents, anchor on `mEngineMaxRPM`: a car parked in the pits reads exactly 0
for `mEngineRPM`.

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
Unlike the plugin, LMU has **104** vehicle slots, not 128, and the block has **no
`mVersionUpdateBegin` / `mVersionUpdateEnd` counters**. A reader can therefore copy a
frame while the game is halfway through writing it. If that matters, check that
`mElapsedTime` has not changed during the copy.

### Finding the player's car

```c
const LMUTelemetryData* td = &obj->telemetry;
if (td->playerHasVehicle && td->playerVehicleIdx < td->activeVehicles)
    t = &td->telemInfo[td->playerVehicleIdx];
```

`playerVehicleIdx` indexes `telemInfo` directly, and lmu-pitwall uses the same guard
as above. It indexes `vehScoringInfo` too: in the dumps, `telemInfo[i]` and
`vehScoringInfo[i]` are the same car for every slot (same `mID`, same vehicle name),
and `vehScoringInfo[playerVehicleIdx].mIsPlayer` is set. This is the opposite of the
rF2 plugin, where the two arrays are in different orders and a reader has to search
for the player.

**`mID` is not the slot index.** lmu-rpm-leds states that `telemInfo[i].mID == i`, but
in a 35-car session the player's car was slot 34 with `mID` 0, and every other slot
`i` had `mID` `i + 1`. Use `playerVehicleIdx` and never search by `mID` assuming it
equals the slot.

### Other cars in the session

The player's car is always fully populated. **Some AI cars are simulated at reduced
detail**, and for those LMU leaves these values at zero: contact-patch and ground
velocities, camber, toe, tyre pressure and temperatures, engine water and oil
temperatures, steering torque, and the driver-aid settings. Wheel rotation, loads,
forces, wear, brake temperature and suspension are still filled in. In the on-track dumps, 9 or 10
of the 34 AI cars were affected.

### Vehicle telemetry (`LMUVehicleTelemetry`, 1888 bytes)

The offsets are relative to the start of one `telemInfo[]` element. Units are S397's
comments from the header unless a row says otherwise.

| Offset | Type | Field | Units / meaning |
|---:|---|---|---|
| 0 | i32 | `mID` | slot ID |
| 4 | f64 | `mDeltaTime` | s since last update |
| 12 | f64 | `mElapsedTime` | session time, s; updated at 100 Hz (`mDeltaTime` reads 0.0100) |
| 20 | i32 | `mLapNumber` | current lap |
| 32 | char[64] | `mVehicleName` | |
| 96 | char[64] | `mTrackName` | |
| 184 | vec3 | `mLocalVel` | m/s, vehicle coordinates; **driving forwards gives negative `z`** (see [Coordinates](#coordinates)) |
| 208 | vec3 | `mLocalAccel` | m/s², vehicle coordinates |
| 232 | vec3[3] | `mOri` | rows of the orientation matrix |
| 304 | vec3 | `mLocalRot` | rad/s, vehicle coordinates |
| 352 | i32 | `mGear` | **-1 = reverse, 0 = neutral, 1+ = forward** (0 and forward gears seen in dumps; reverse not yet) |
| 356 | f64 | `mEngineRPM` | 0 when parked, so never use it to locate the block |
| 364 / 372 | f64 | `mEngineWaterTemp` / `mEngineOilTemp` | °C |
| 388 … 412 | f64 ×4 | `mUnfilteredThrottle`, `…Brake`, `…Steering`, `…Clutch` | 0..1; steering -1 (left) .. 1 (right) |
| 420 … 444 | f64 ×4 | `mFilteredThrottle`, `…Brake`, `…Steering`, `…Clutch` | filtered inputs, same ranges |
| 452 | f64 | `mSteeringShaftTorque` | torque around the steering shaft |
| 484 / 492 | f64 | `mFrontRideHeight` / `mRearRideHeight` | **always 0 in LMU** (seen in every dump, on track too); use the per-wheel `mRideHeight`. `mDrag`, `mFrontDownforce` and `mRearDownforce` are also always 0 |
| 524 | f64 | `mFuel` | litres |
| 532 | f64 | `mEngineMaxRPM` | rev limit; safe anchor for locating the block |
| 600 | i32 | `mCurrentSector` | zero-based; sign bit set = in pit lane (reads `0x80000000` in the garage) |
| 604 | u8 | `mSpeedLimiter` | |
| 605 | u8 | `mMaxGears` | forward gears |
| 608 | f64 | `mFuelCapacity` | litres |
| 664 | f64 | `mRearBrakeBias` | fraction of braking on the rear |
| 672 | f64 | `mTurboBoostPressure` | **Pa, absolute**: reads 101325 (1 atm) on a naturally aspirated car. The header gives no unit |
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
| 48 / 56 | f64 | `mLateralPatchVel` / `mLongitudinalPatchVel` | velocity at the contact patch, m/s (no unit in the header). Small, about ±0.4 at 27 m/s; positive longitudinal on driven wheels under power |
| 64 / 72 | f64 | `mLateralGroundVel` / `mLongitudinalGroundVel` | ground velocity at the patch, m/s, in vehicle axes: longitudinal equals `mLocalVel.z` (forwards is negative). The header's comment repeats the patch-velocity text |
| 80 | f64 | `mCamber` | rad |
| 88 / 96 | f64 | `mLateralForce` / `mLongitudinalForce` | N |
| 104 | f64 | `mTireLoad` | N |
| 112 | f64 | `mGripFract` | approximate fraction of the patch that is sliding |
| 120 | f64 | `mPressure` | kPa |
| 128 | f64[3] | `mTemperature` | K; left / centre / right (not inside / outside) |
| 152 | f64 | `mWear` | **1.0 = new**, falling with wear (1.00000 on unused tyres) |
| 160 | char[16] | `mTerrainName` | material prefix from the track's TDF file |
| 176 | u8 | `mSurfaceType` | 0 dry, 1 wet, 2 grass, 3 dirt, 4 gravel, 5 rumble strip, 6 special |
| 177 / 178 | bool | `mFlat` / `mDetached` | |
| 179 | u8 | `mStaticUndeflectedRadius` | **tyre radius in centimetres**, whole centimetres only. It can change: the same rear tyre read 36 in the garage and 37 on track |
| 180 | f64 | `mVerticalTireDeflection` | how far the tyre is compressed from its speed-dependent radius, m (about 0.010 to 0.014 in the dumps; no unit in the header) |
| 188 | f64 | `mWheelYLocation` | relative to the vehicle's y |
| 196 | f64 | `mToe` | rad |
| 204 | f64 | `mTireCarcassTemperature` | K |
| 212 | f64[3] | `mTireInnerLayerTemperature` | K |
| 236 | | 1.3 additions / reserved | see below |

The tyre radius is given, so a reader does not have to estimate wheel size from
wheel speed. A cross-check from the on-track dump: the front wheels turned at
76.07 rad/s at 27.39 m/s, giving 0.360 m, and their `mStaticUndeflectedRadius` was 36.
Slip ratio can then be computed from rotation, radius and speed.

**The patch velocities are not yet a usable slip ratio.** Dividing
`mLongitudinalPatchVel` by `mLongitudinalGroundVel` gives the expected signs (negative
on driven wheels under power), but not the expected sizes. In the on-track dump the
driven rear wheels came out at 1.3% wheelspin while rotation and radius gave 2.4% to
6%, and the free-rolling fronts showed 1.6% of braking slip. What exactly the patch
velocity measures is not documented, so simapi does not publish a slip ratio for
LMU. Slip angle from the ground velocities, `atan(lateral / longitudinal)`, is what
TinyPedal uses.

**In the garage several values are zero:** tyre pressure and all tyre temperatures,
engine water and oil temperatures, and RPM. Brake temperature, wear, loads,
suspension and radius are filled in.

**Pausing is not signalled.** While the game is paused, `mInRealtime` stays 1 and
`mGamePhase` stays 5 (green flag). The block simply stops changing, with
`mElapsedTime` frozen and RPM and speed held at their last values (seen live in
LMU 1.4.3). The only way to detect a pause is a clock that stops advancing.

### LMU 1.3 additions

These fields occupy bytes that `include/lmu.h` still declares as `mExpansion`. They
fit those bytes exactly, so every older offset is unchanged.

Vehicle telemetry (replacing `mExpansion[103]` at 745; `mExpansion[20]` remains at 828):

| Offset | Type | Field | Meaning |
|---:|---|---|---|
| 745 | bool | `mLapInvalidated` | |
| 746 | bool | `mABSActive` | **ABS intervening right now** |
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

`include/lmu.h` and pyLMUSharedMemory also name five fields inside the 48 reserved
bytes of the rF2 vehicle scoring record: `mSteamID` u64 at 536, `mVehFilename`
char[32] at 544, `mAttackMode` u16 at 576, `mFuelFraction` u8 at 578 and
`mDRSState` bool at 579.

### Coordinates

From S397's rF2 `InternalsPlugin.hpp`, which LMU inherits. The world coordinate
system is left-handed with +y up. In vehicle coordinates:

- **+x points out of the left side** of the car (from the driver's point of view)
- **+y points out of the roof**
- **+z points out of the back**, so **driving forwards gives a negative `mLocalVel.z`**

Rotations: +x pitches up, +y yaws to the right, +z rolls to the right. Compared with
ISO vehicle coordinates (+x forward, +y right, +z up), a -z velocity in rF2 is a +x
velocity in ISO. LMU follows this: a car driving forwards at 27.39 m/s had
`mLocalVel` = (-0.002, 0.036, -27.390), and accelerating gave a negative
`mLocalAccel.z`. The rotation conventions have not been checked.

## Known discrepancies

| Field | What the header says | What readers do | Status |
|---|---|---|---|
| `mBrakeTemp` | "Celsius" | TinyPedal (both its LMU and rF2 readers) and lmu-pitwall treat it as Kelvin | **Kelvin**: cold brakes in the garage read 289.60 with ambient at 16.00 °C (289.15 K) |
| `mWear` | "0.0-1.0, fraction of maximum" | lmu-pitwall says 1.0 = new; TinyPedal returns it unchanged | **1.0 = new**: 1.00000 on unused tyres, 0.9988 to 0.9995 after part of a lap |
| `mTurboBoostPressure` | no unit | lmu-pitwall assumes bar | **Pa, absolute**: 101325 on a naturally aspirated car |
| `mRotation` | rad/s, no sign | simapi's rF2 mapper negates it | **negative forwards**, so negate it for forward-positive wheel speed |
| `mID` | slot ID | lmu-rpm-leds says `telemInfo[i].mID == i` | **false**: see [Finding the player's car](#finding-the-players-car) |
| `mFrontRideHeight`, `mRearRideHeight`, `mDrag`, `mFrontDownforce`, `mRearDownforce` | documented | | **always 0** in LMU |
| scoring `mSectorFlag` | rF2 meaning | lmu-pitwall observed **1 = yellow, 11 = clear** in LMU | observed by lmu-pitwall only |

## Verification status

**The layout has been cross-checked offline.** Every field's offset and size was
extracted mechanically from four independent definitions:

1. simapi `include/lmu.h`, compiled with gcc (Linux) and with mingw-w64 (Windows ABI)
2. pyLMUSharedMemory `lmu_data.py`, translated to C and compiled
3. lmu-pitwall's Rust structs, compiled, using `offset_of!`
4. S397's rF2 `InternalsPlugin.hpp` (for the embedded rF2 records), compiled with
   mingw-w64 so that `long` is 4 bytes

Every field that two or more of these define has the same offset and size in all of
them; a few have different names in different sources (e.g. `mWheel` / `mWheels`).
The only differences are the reserved blocks that the 1.3 revision splits into named
fields. lmu-rpm-leds independently gives `mGear` 352, `mEngineRPM` 356,
`mUnfilteredThrottle` 388 and `mEngineMaxRPM` 532, which match. lmu-pitwall reports
its definitions were checked against a running LMU 1.4.

**Checked against real data.** Four dumps of `LMU_Data` were taken from a game
reporting `gameVersion` 14200, under GE-Proton10-34-LMU, by copying winecarte's
`/dev/shm/LMU_Data`. They were taken at Circuit de Barcelona (practice, 35 cars, Aston Martin
Valkyrie LMH, 16 °C ambient): in the main menu, in the garage, on track at 27 m/s in
second gear, and stopped on track after braking. Each copy was 327,680 bytes, which
is the 324,820-byte layout rounded up to whole 4 KiB pages. Decoded with
`include/lmu.h`, every string landed exactly in place, and every field that should
be live read a plausible value. That includes the LMU 1.3 fields:
`mSessionTimeRemaining` equals `mEndET − mCurrentET` exactly, `mVehicleModel` reads
"Aston Martin Valkyrie LMH", and `mCompoundType` matches the "Medium" compound name.
Telemetry slots at `activeVehicles` and above were entirely zero.

A plausibility sweep over all 35 cars checked every value against physical ranges
and against the independently written scoring block. That included `mID` and names
matching between the arrays, telemetry and scoring positions agreeing within 0.2 s of
travel, and every orientation matrix having determinant ±1. Everything passed,
apart from two expected findings: one AI wheel briefly off the ground (small negative
load), and the reduced-detail AI cars described under
[Other cars](#other-cars-in-the-session).

**Still not seen in real data:** reverse gear, an ABS intervention (`mABSActive` was
always 0, and this car has no ABS), the rotation sign conventions, and values from
any other car or track.

S397's `SharedMemoryInterface.hpp` itself is not redistributed and was not consulted
directly. The LMU-specific field names, types and comments here come from
pyLMUSharedMemory and lmu-pitwall, both of which state that they were derived from it.
The rF2 parts were also checked against S397's rF2 `InternalsPlugin.hpp`.

## Sources

| Source | Revision | Used for |
|---|---|---|
| [TinyPedal/pyLMUSharedMemory](https://github.com/TinyPedal/pyLMUSharedMemory) `lmu_data.py`, `lmu_mmap.py` | `6a8dd831` (2026-10-14) | full 1.3 layout, S397 field comments, Linux file handling |
| [Swizzjack/lmu-pitwall](https://github.com/Swizzjack/lmu-pitwall) `bridge/src/shared_memory/`, `tools/lmu-probe/` | `a171b80a` (2026-08-03) | size assertions, the Enable Plugins requirement, live findings for LMU 1.4 |
| [TinyPedal/TinyPedal](https://github.com/TinyPedal/TinyPedal) `tinypedal/adapter/lmu_reader.py` | `ad8e84e5` (2026-10-17) | unit conversions in practice |
| [TheIronWolfModding/rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin) `Include/InternalsPlugin.hpp` | `48aa12db` (2024-12-31) | S397 rF2 base structs, coordinate system |
| [fpauker/lmu-rpm-leds](https://github.com/fpauker/lmu-rpm-leds) README | `e0802f16` (2026-10-02) | offsets, memfd access, player index, pitfalls |
| [srounce/winecarte](https://github.com/srounce/winecarte) `crates/wine2linux`, `crates/winecarte-core/src/games.rs` | `ffca3b5f` (2026-10-10) | copy mechanism, update event, lock objects, file size |
| [spacefreak18/simshmbridge](https://github.com/spacefreak18/simshmbridge) `Makefile.lmu`, `simshmbridge.c` | `37d2f082` (2026-06-08) | Linux bridge mechanism |
| [TinyPedal discussion #9](https://github.com/TinyPedal/TinyPedal/issues/9) | | crash when the mapping is too small |
| [LMU V1.2 changelog](https://www.bsimracing.com/le-mans-ultimate-update-v1-2-full-changelog/), [V1.4.2 notes](https://traxion.gg/le-mans-ultimate-v1-4-2-patch-notes-over-90-new-liveries-and-online-fixes-dominate-new-update/) | | version history |
