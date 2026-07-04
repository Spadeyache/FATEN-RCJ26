# processing/

**Rule:** Turn raw sensor data into clean, decision-ready state variables.

## What belongs here

- Filtering (vote filters, EMA smoothing, debouncers).
- Frame parsing of multi-byte protocols (e.g. K230 detection frames).
- Coordinate transforms (sensor frame → robot frame → world frame).
- Sensor fusion (EKF, scan-matching, multi-sensor agreement).
- Stateful interpretation that needs history (e.g. "line lost for 3 frames").

## What does NOT belong here

- Direct I/O — go through `sensors/`.
- Policy/decisions about what the robot should *do* (that's `state_machine/`).
- Motion commands (`actions/`).

## Namespace

`namespace Processing::<Module> { … }`. Each module exposes `tick()` (if it
needs periodic work) plus typed getters.

## Layer rule

Processing may call `sensors/` and `drivers/`. It must not include from
`actions/` or `state_machine/`.

## Files

| File | Role |
|---|---|
| `CommandFilter.{h,cpp}` | Majority-vote ring buffer over XIAO feature byte |
| `XiaoDecode.{h,cpp}` | Reads XIAO_link cache → `xiaoCommand`, `lineError` |
| `K230Decode.{h,cpp}` | Stream parser for K230D frames → `detections[]` |
| `Pose.{h,cpp}` | 3-state EKF (x, y, θ) |
| `Mapping.{h,cpp}` | Pose estimator (Phase 0): predict + yaw update |
