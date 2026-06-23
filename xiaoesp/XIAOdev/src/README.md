# XIAOdev Source Layout

- `../XIAOdev.ino`: main Arduino entry point, task setup, frame loop, USB stream framing.
- `config/`: shared compile-time configuration and serial print switches.
- `drivers/`: hardware or board-facing wrappers, such as camera, WiFi, and Teensy UART framing.
- `modes/`: top-level robot behavior modes called from `XIAOdev.ino`.
- `processing/`: reusable vision and line-processing helpers called by modes.
- `stream/`: PC viewer/debug stream state, formatting, and the Web Serial HTML viewer.

Keep mode-specific tuning constants inside the mode file unless another mode
really shares them. Shared constants belong in `config/config.h`.
