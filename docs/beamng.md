# BeamNG.drive

BeamNG.drive sends telemetry over UDP. simapi listens on port **30000** and
understands two of BeamNG's protocols, which can share that port:

- **OutGauge**: dashboard data (rpm, speed, gear, pedals including clutch,
  turbo, fuel level, handbrake light).
- **MotionSim**: chassis data (world position and velocity, local velocity,
  heading, pitch and roll).

## Setup

In BeamNG.drive, open Options > Other > Protocols and set:

| Protocol | Enabled | IP | Port |
|---|---|---|---|
| OutGauge UDP protocol | yes | 127.0.0.1 | 30000 |
| MotionSim UDP protocol | yes | 127.0.0.1 | 30000 |

If simd runs on another machine, use that machine's IP address instead of
127.0.0.1. MotionSim is optional; without it, only the OutGauge fields are
available.

## What is mapped

| Data | Source | Notes |
|---|---|---|
| rpm, speed, throttle, brake, clutch | OutGauge | max rpm is the highest rpm seen so far |
| gear, gear text | OutGauge | |
| turbo | OutGauge | in bar |
| fuel | OutGauge | fraction of the tank (0 to 1), not litres |
| handbrake | OutGauge | 1 while the handbrake light is on |
| world position and velocity | MotionSim | Z is up |
| local velocity (sideways, forward, vertical) | MotionSim | |
| heading, pitch, roll | MotionSim | degrees |

BeamNG doesn't send wheel data (wheel speed, tyre size, suspension) in
either protocol, so effects that need it (tyre slip, tyre lock, ABS,
suspension) aren't available. Engine rpm and gear shift effects work.

The car name is always "beam", as BeamNG's OutGauge doesn't send the
vehicle's name.
