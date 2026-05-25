# STS-imuMerge — Teensy Firmware

State-machine line-follow + evacuation robot firmware. This README describes
**how the code is organised** so any new file lands in the right place without
guesswork.

## Top-level files

| File | Role |
|---|---|
| `STS-imuMerge.ino` | Arduino entry: `setup()`, `loop()`. Loop just calls `StateMachine::tick()`. Nothing else here. |
| `config.h` | All tunable constants (PID gains, timings, thresholds) + `PRINT_*` flags. **No pin numbers.** |
| `pins_teensy.h` | Pin definitions for this MCU (Teensy 4.1). Only pins live here. |
| `README.md` | This file. |

## Layered architecture

Calls flow **downward only**. A layer never calls a layer above it.

```
  state_machine/    ← orchestrates behaviour
       │ uses
       ▼
   actions/         ← blocking high-level motions (turn, forward, arm)
       │ uses
       ▼
  processing/       ← sensor fusion + filtering (CommandFilter, Pose/EKF, Map)
       │ uses
       ▼
   sensors/         ← raw I/O wrappers (XIAO link, K230 link, IMU, ToF, touch)
       │ uses
       ▼
   drivers/         ← per-chip register-level wrappers (yacheSTS, yacheMPU6050…)
```

Every `src/<layer>/` folder has its own `README.md` with that layer's rules.

## State machine

States live in `src/state_machine/<STATE>.{h,cpp}` — one file pair per state.
Each state is a C++ namespace exposing `onEnter()` and `update()`. A state
ends its `update()` by optionally calling `StateMachine::transitionTo(NEXT)`.

```
LINE_Follow  ──► LINE_Gap       (xiaoCommand == 8)
LINE_Follow  ──► LINE_Obstacle  (touchfront)
LINE_Follow  ──► STALLED_RED    (xiaoCommand == 4; handled inline in StateMachine)
LINE_Follow  ──► EVAC_Entry     (xiaoCommand == 5)

EVAC_Entry   ──► EVAC_Search ──► EVAC_Deploy ──► EVAC_Exit ──► LINE_Follow
```

## Where new code goes

| Adding… | Goes in… |
|---|---|
| A new sensor chip wrapper | `src/drivers/` |
| Reading raw bytes from a sensor over I²C/UART | `src/sensors/` |
| Decoding/filtering/fusing those bytes | `src/processing/` |
| A blocking motion primitive (e.g. spin in place) | `src/actions/` |
| A new robot behaviour mode | `src/state_machine/` (new state) |
| A tuning constant | `config.h` |
| A pin number | `pins_teensy.h` |

## Print toggles

Each module's `Serial.printf` calls are gated by a `PRINT_*` flag in `config.h`.
Set the flag to `0` to silence that module without touching its code.

## Build

Arduino CLI auto-recurses `src/`, so the layered folders compile out of the
box. No `library.properties` needed.
