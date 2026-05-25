# state_machine/

**Rule:** One file pair per top-level robot mode. Each state file is a namespace
exposing `onEnter()` and `update()`. The state machine itself (in
`StateMachine.cpp`) holds the active-state variable and a `switch` that
dispatches to the right `update()` every loop tick.

## How a state ends

At the end of its `update()`, a state may call:

```cpp
StateMachine::transitionTo(NEW_STATE);
```

The transition is applied on the next loop iteration. `onEnter()` runs once
when a state becomes active.

## Transition graph

```
                 ┌────────────► STALLED_RED ─┐
                 │   xiaoCmd==4              │ (cleared by XIAO)
                 │                           ▼
LINE_FOLLOW ◄────┴──────────────────────► LINE_FOLLOW
   │
   ├─ touchfront ──► LINE_OBSTACLE ──► LINE_FOLLOW
   │
   ├─ xiaoCmd==8 ──► LINE_GAP      ──► LINE_FOLLOW
   │
   └─ xiaoCmd==5 ──► EVAC_ENTRY ──► EVAC_SEARCH ──► EVAC_DEPLOY ──► EVAC_EXIT ──► LINE_FOLLOW
```

`STALLED_RED` has no file — it is two lines inside `StateMachine.cpp`'s switch
(motors off until xiaoCommand clears).

## What belongs here

- Mode-level orchestration: "if A and not B, then call Actions::Turn(…)".
- Per-mode cooldowns and one-shot flags (e.g. `disableGreen` after a green turn).

## What does NOT belong here

- Math, filtering, or fusion (`processing/`).
- Motion primitives (`actions/`).
- Raw I/O (`sensors/`, `drivers/`).

## Namespace

`namespace <STATE_NAME> { void onEnter(); void update(); }`

## Files

| File | Role |
|---|---|
| `StateMachine.{h,cpp}` | Enum `RobotState`, `init/tick/transitionTo`, STALLED_RED inline |
| `LINE_Follow.{h,cpp}` | PID + intersection/green-turn dispatch |
| `LINE_Obstacle.{h,cpp}` | Front-bumper avoidance sequence |
| `LINE_Gap.{h,cpp}` | Line-gap recovery (xiaoCommand == 8) |
| `EVAC_Entry.{h,cpp}` | Verbatim entry sequence; transitions to EVAC_Search at end |
| `EVAC_Search.{h,cpp}` | Skeleton — runs Processing::Mapping::tick() + heartbeat |
| `EVAC_Deploy.{h,cpp}` | Skeleton — future drop logic |
| `EVAC_Exit.{h,cpp}` | Skeleton — future zone-exit logic |
