# Line-Follow Logic Specification (deploy-run variant)

Implementation-agnostic description of the line-following state logic.
Written so any team can implement it on their own robot/codebase.

## 1. Assumptions about the robot

- Differential (skid-steer) drive; can do in-place spins by a relative angle
  (timed or gyro-based — this spec doesn't care which).
- A camera module that, per frame, reports **line events** and a **line error**
  (signed offset of the black line from image center) for PID steering.
- Raw camera events are **debounced** (e.g. rolling vote window) into confirmed
  events before this logic sees them. The debounce filter can be **cleared**
  (all votes wiped) after a blocking maneuver so stale frames don't re-fire.
- A **front bumper** switch (true = pressed).
- Two payload release mechanisms: a **LEFT arm** and a **RIGHT arm** (e.g.
  servo grippers holding rescue kits). Each can be released independently.
- Confirmed events used here: `GREEN_LEFT`, `GREEN_RIGHT`, `SILVER`,
  `BLACK_INTERSECT` (saturated black row / plain intersection), `NONE`.

No IMU / slope handling — this logic is for a flat course only.

## 2. Tunable constants

| Constant | Example | Meaning |
|---|---|---|
| `GREEN_RIGHT_TURN_COUNT_LEFT` | 1 | Nth right-green that fires a deploy turn and selects the **LEFT arm** for the next wall touch |
| `GREEN_RIGHT_TURN_COUNT_RIGHT` | 2 | Same, selects the **RIGHT arm**. **No ordering guarantee** — LEFT may be larger or smaller than RIGHT |
| `DEPLOY_TOUCH_STOP_MS` | 6000 | Stop duration at the deploy wall |
| `END_RIGHT_GREEN_STOP_MS` | 12000 | Stop duration at the end marker |
| `DISABLE_GREEN_MS` | 1000 | Green-ignore cooldown after any green turn |
| `BLACK_INTERSECT_COOLDOWN_MS` | ~1100 (clamp 700–2000) | Green-ignore cooldown after driving straight through an intersection (scale by drive speed if you like; a fixed ~1100 ms is fine) |
| Green turn motion | fwd ~52 mm then ±90° spin | The "green movement" (see §5) |
| 180° turn speed | 60 % | Speed for the deploy turnaround |

## 3. Persistent state (lives for the whole run, reset only at power-on)

| Variable | Init | Purpose |
|---|---|---|
| `greenRightCount` (int) | 0 | How many confirmed right-greens have been acted on |
| `pendingArm` (NONE/LEFT/RIGHT) | NONE | Which arm the next front-touch releases; set by a deploy turn |
| `firstLeftGreenDone` (bool) | false | The very first left-green of the run is special (see §4.4) |
| Green cooldown (bool + timestamp + duration) | off | While active, ALL green events are ignored (robot just PIDs) |

Helper predicate used below:

```
bothDeployCountsPassed() :=
    greenRightCount >= GREEN_RIGHT_TURN_COUNT_LEFT
 && greenRightCount >= GREEN_RIGHT_TURN_COUNT_RIGHT
```

## 4. Main loop (priority order, checked every tick)

### 4.0 Cooldown expiry
If the green cooldown is active and its duration has elapsed, clear it.

### 4.1 Front bumper pressed → deploy sequence (highest priority, always active)
1. Stop the motors.
2. Release the pending arm: LEFT arm if `pendingArm == LEFT`, RIGHT arm if
   `RIGHT`, nothing if `NONE`. Then set `pendingArm = NONE`.
3. **Course-specific hack:** if `greenRightCount == 3` exactly, increment it to
   4 (only the 3 → 4 transition; no other value is bumped).
4. Stay stopped for `DEPLOY_TOUCH_STOP_MS` (keep pumping camera I/O while
   waiting so the link stays alive).
5. Spin 180° in place.
6. Clear the vision debounce filter, arm the green cooldown
   (`DISABLE_GREEN_MS`), resume line following.

### 4.2 `GREEN_RIGHT` event
If green cooldown active → just run the line PID (ignore the event).
Otherwise:

- **If `bothDeployCountsPassed()` is already true** (before incrementing):
  this is the **END marker**. Stop the motors, beep, stay stopped for
  `END_RIGHT_GREEN_STOP_MS`, clear filter, arm green cooldown, resume.
  (The count is NOT incremented; every further right-green repeats this.)

- **Else**: increment `greenRightCount`, then:
  - If the new count `== GREEN_RIGHT_TURN_COUNT_LEFT` → **deploy turn**:
    set `pendingArm = LEFT`, execute the green-RIGHT movement (§5),
    stop, clear filter, arm green cooldown (`DISABLE_GREEN_MS`).
  - Else if the new count `== GREEN_RIGHT_TURN_COUNT_RIGHT` → same but
    `pendingArm = RIGHT`.
  - Else (any other count) → **drive straight through**: forward the same
    distance/speed the turn would have used (no turn), stop, clear filter,
    arm the longer black-intersect cooldown so the marker isn't re-read
    on the way out.

  (If both trigger counts are set to the same number, LEFT wins.)

### 4.3 `GREEN_LEFT` event
If green cooldown active → just run the line PID.
Otherwise decide the direction:

```
turnLeft := (NOT firstLeftGreenDone) OR bothDeployCountsPassed()
firstLeftGreenDone := true
```

- `turnLeft == true`  → execute the green-LEFT movement (§5) — normal marker.
- `turnLeft == false` → execute the green-RIGHT movement instead. Rationale:
  before both deploys are done, a left-green is assumed to be the deploy
  intersection seen **mirrored on the way back from the wall**, and turning
  right resumes the original course direction.

Then stop, clear filter, arm green cooldown (`DISABLE_GREEN_MS`).

Summary table:

| Left-green # | bothDeployCountsPassed | Action |
|---|---|---|
| 1st of run | any | turn LEFT |
| 2nd+ | false | turn RIGHT |
| 2nd+ | true | turn LEFT |

### 4.4 `SILVER` event
Stop and hand off to the evacuation-zone state machine (out of scope here).

### 4.5 `BLACK_INTERSECT` event (saturated black row, plain intersection)
Short beep, arm the black-intersect green cooldown, clear the filter, and keep
running the line PID (no maneuver — go straight by following the line).

### 4.6 No event
Run the line-follow PID: `steer = Kp*err + Kd*d(err)/dt` around a fixed base
speed, wheels = base ± correction. (Any PID you already have is fine.)

## 5. The "green movement" (intersection turn macro)

Used by both green directions; blocking:

1. Drive forward a fixed distance (~52 mm) at line-follow base speed, to put
   the wheel axis over the intersection center.
2. Spin in place toward the marker side. Recommended: do a timed/gyro spin of
   (90° − finish budget), then finish by rotating until the camera sees the
   black line centered ahead, capped by the remaining budget (~35°) as a
   timeout. A plain 90° spin also works, just less self-correcting.
3. Resume normal line following.

The "straight through" variant is step 1 only (no spin).

## 6. Cooldown semantics (important for not double-counting)

- While the green cooldown is active, green events don't just skip the
  maneuver — they are **not counted at all** (the counters/flags are only
  touched when an event actually fires an action).
- After ANY green action (turn, straight-through, deploy touch, end stop):
  clear the vision debounce filter first, then arm the cooldown. Both are
  required: clearing kills the stale votes, the cooldown covers re-seeing the
  same marker while driving away from it.

## 7. Intended course flow (why the rules are what they are)

The course is known in advance. Right-green markers are numbered by encounter
order. Two of them (numbers `COUNT_LEFT` and `COUNT_RIGHT`) lead into dead-end
spurs ending at a wall where a payload must be dropped from a specific arm:

1. Robot counts right-greens, driving straight through the ones that don't
   matter.
2. At marker #`COUNT_LEFT` (or `#COUNT_RIGHT`) it turns right into the spur,
   drives until the bumper hits the wall, drops the corresponding arm's
   payload, waits 6 s, turns 180°, and drives back.
3. Rejoining the main line, the same intersection now appears as a LEFT green;
   the mirror rule (§4.3) turns it into a right turn so the robot resumes its
   original direction. (The one genuine left-green early on the course is
   exempted via `firstLeftGreenDone`.)
4. After both deploys are done, the next right-green is the finish marker:
   the robot stops there for 12 s.
