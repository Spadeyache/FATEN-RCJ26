# sensors/

**Rule:** Raw I/O only. Bytes in, bytes out. **No interpretation.**

## What belongs here

- One module per source of input (XIAO link, K230 link, IMU, ToF, touch pins).
- Each module owns the driver instance(s) it needs and exposes `init()` +
  `tick()` and the rawest possible getters (`uint8_t getRegister(id)`,
  `int readByte()`, `float getYaw_deg()`, `bool isPressed()`).
- The job is to keep the I/O serviced and the cache fresh — that's it.

## What does NOT belong here

- Filtering, fusion, debouncing, frame parsing, coordinate transforms.
  All of that lives in `processing/`.
- Decisions about *what* the data means. A sensor module reports "yaw = 12.3°";
  the consumer decides whether that's "drifting left" or "going straight".

## Naming + namespace

Each file declares `namespace Sensors::<Module> { void init(); void tick(); … }`.

## Layer rule

Sensors call **drivers/** only. They never include from `processing/`,
`actions/`, or `state_machine/`.

## Files

| File | What it owns |
|---|---|
| `XIAO_link.{h,cpp}` | `YacheEncodedSerial` instance on Serial3 + per-register cache |
| `K230_link.{h,cpp}` | Serial5 byte stream (no parsing) + idle/detect command tx |
| `IMU.{h,cpp}` | `yacheMPU6050` on Wire1 + pitch/roll/yaw getters |
| `ToF.{h,cpp}` | `yacheVL53L7CX` array + dataReady/getRanges passthrough |
| `Touch.{h,cpp}` | Digital pins: `touchfront`, `conduct0`, `conduct1` |
