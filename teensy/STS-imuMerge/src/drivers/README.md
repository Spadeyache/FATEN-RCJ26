# drivers/

**Rule:** One file pair per physical chip. Talks the chip's own protocol —
nothing else.

## What belongs here

- Register reads/writes (I²C, SPI, UART) to a single device.
- Conversion of raw register values into engineering units (g, °/s, mm, etc.).
- Per-chip initialisation, calibration helpers, datasheet quirks.

## What does NOT belong here

- Cross-chip fusion (that's `processing/`).
- Robot-frame coordinate transforms (that's `processing/` or `sensors/ToF`).
- High-level motion (`actions/`).
- Decisions about *when* to read the chip (`sensors/` does that).

## Naming

`yache<ChipName>.{h,cpp}`. The `yache` prefix marks in-house wrappers (vs.
upstream library drivers like `MPU6050`, `VL53L7CX`, `SCServo`).

## Layer rule

Drivers may call **upstream libraries only**. Never include anything from
`sensors/`, `processing/`, `actions/`, or `state_machine/`.

## Files

| File | Chip |
|---|---|
| `yacheSTS.{h,cpp}` | Feetech STS smart servo (drive motors), UART |
| `yacheMPU6050.{h,cpp}` | InvenSense MPU-6050 IMU + Madgwick filter, I²C |
| `yacheVL53L7CX.{h,cpp}` | ST VL53L7CX ToF (8×8 multizone), I²C |
| `yacheEncodedSerial.{h,cpp}` | 3-byte [255, id, val] register protocol over UART |

Hobby servos (HS-45HB grab) and the KRS lift servo use the upstream `Servo` and
`IcsHardSerialClass` libraries directly from `actions/Arm.cpp` — they're thin
enough that an extra wrapper would only add noise.
