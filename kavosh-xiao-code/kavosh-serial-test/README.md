# kavosh serial test (RX7 / TX7)

Minimal two-board "hello test" over UART, using the same link logic as the
faten integration (`xiaoesp/XIAOdev` ↔ `teensy/STS-imuMerge`) but on the
Teensy's **Serial7** instead of Serial3.

Each board sends `hello test` once a second and prints whatever it receives on
its USB serial console — so you can confirm **both** RX and TX are wired right.

## Wiring

| XIAO ESP32-S3 | Teensy 4.1     | note                    |
|---------------|----------------|-------------------------|
| `D6` (TX)     | pin 28 (RX7)   | XIAO → Teensy           |
| `D7` (RX)     | pin 29 (TX7)   | Teensy → XIAO           |
| `GND`         | `GND`          | common ground required  |

XIAO pins match `XIAOdev/src/config/hardware_config.h`
(`SERIAL_TEENSY_TX_PIN D6`, `SERIAL_TEENSY_RX_PIN D7`).

## Sketches

- `xiao_serial_test/xiao_serial_test.ino` — flash to the XIAO ESP32-S3.
- `teensy_serial_test/teensy_serial_test.ino` — flash to the Teensy 4.1.

Open both USB serial monitors at **115200**. You should see `got from ...`
lines appearing once a second on each side.

## Notes

- **Baud**: set to `115200` here for easy probing. The real faten link runs at
  `4000000` — bump `LINK_BAUD` in **both** sketches together if you want to test
  at full speed.
- This test uses plain text lines. To exercise the real 3-byte
  `[255, id, value]` register protocol instead, drop in the shared
  `yacheEncodedSerial` driver (see `src/drivers/yacheEncodedSerial.*` in either
  folder) and construct it on `Serial7` / `Serial1`.
