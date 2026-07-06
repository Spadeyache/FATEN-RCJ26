# Kavosh/Faten Cooperative Deploy Test

Ordered deploy numbers are packed as decimal digits in the same order the colors
were sent.

- `orange,silver -> 31` means orange deploys at right-green count 3 and silver at 1.
- `silver,orange -> 13` is the same mapping for the opposite color order.
- If Kavosh only has one color, the unused digit is `0`, for example `30`.

Files:

- `kavosh_xiao_coop/kavosh_xiao_coop.ino`: Kavosh XIAO ESP-NOW/UART bridge.
- `kavosh_teensy_coop/kavosh_teensy_coop.ino`: minimal Teensy-side example.

UART wiring matches the Faten XIAO link style: XIAO `D6` TX to Teensy RX,
XIAO `D7` RX to Teensy TX, and common GND.
