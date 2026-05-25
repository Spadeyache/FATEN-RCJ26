# actions/

**Rule:** High-level motions/manoeuvres a state can invoke. Drive primitives,
turns, forward moves, arm motions all live here.

## What belongs here

- A function or namespace that performs **one robot motion** with a clear
  beginning and end — `turn(80°)`, `forward(100mm, useIMU=true)`,
  `armGrab(closed)`, `armLift(pos)`.
- Owns the four-wheel gain state + the control-loop ISR (`Drive`).
- Owns the hobby-servo + KRS objects (`Arm`).

## Blocking vs. non-blocking

All actions in this layer are **blocking** — they return when the motion is
complete. The state machine is expected to wait. If a state needs to fuse a
motion with concurrent sensing (e.g. "drive forward until touchfront fires"),
it composes that itself by calling `Actions::Drive::motor()` directly and
polling the sensor in a loop.

The old `EXECUTING_TURN` non-blocking state machinery is gone — `Turn::turn()`
is blocking and that's it.

## What does NOT belong here

- Frame parsing, fusion, filtering (`processing/`).
- Raw chip I/O (`drivers/`).
- Decisions about *which* action to run when (`state_machine/`).

## Namespace

`namespace Actions::<Module> { … }`.

## Layer rule

Actions may call `sensors/`, `processing/`, and `drivers/`. They must not
include from `state_machine/`.

## Files

| File | Provides |
|---|---|
| `Drive.{h,cpp}` | `motor(L, R)`, gain accessors, owns `yacheSTS` + `IntervalTimer` |
| `Turn.{h,cpp}` | `turn(degrees)` — signed angle, time-based, blocking |
| `Forward.{h,cpp}` | `forward(speed, dist_mm, useIMU)` — blocking, optional yaw-hold |
| `Arm.{h,cpp}` | `grab(closed)`, `lift(pos)`, owns the Servo + KRS objects |
