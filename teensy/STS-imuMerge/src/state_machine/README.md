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

```text
LINE_FOLLOW -> EVAC -> LINE_FOLLOW
```

Red-stop, line-obstacle, and line-gap states have been removed. Red and
line-loss detections are not emitted by `CommandFilter`, and front-touch no
longer transitions to a line obstacle handler.

## What belongs here

- Mode-level orchestration: "if A and not B, then call Actions::Turn(...)".
- Per-mode cooldowns and one-shot flags, such as `disableGreen` after a green
  turn.

## What does NOT belong here

- Math, filtering, or fusion (`processing/`).
- Motion primitives (`actions/`).
- Raw I/O (`sensors/`, `drivers/`).

## Namespace

`namespace <STATE_NAME> { void onEnter(); void update(); }`

## Files

| File | Role |
|---|---|
| `StateMachine.{h,cpp}` | Enum `RobotState`, `init/tick/transitionTo` |
| `LINE_Follow.{h,cpp}` | PID + intersection/green-turn dispatch |
| `EVAC.{h,cpp}` | Evac victim search/grab and K230D exit-point return |
