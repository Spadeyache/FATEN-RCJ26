# Teensy patch for the new K230 XY protocol

Replaces the COUNT-based multi-detection parser with a fixed 8-byte
single-best-detection parser. Wire as before (Serial5, 115200 baud).

## Wire protocol

K230 → Teensy, 8 bytes per packet:

```
[0xAA] [0x55] [type] [score] [x_hi] [x_lo] [y_hi] [y_lo]
```

- `type`  : 0xFF = no detection; 0 = black; 1 = silver
- `score` : 0-255 (= round(confidence * 255))
- `x, y`  : 16-bit big-endian pixel center in SENSOR coordinates
            (0..sensor_w, 0..sensor_h). The K230 has already inverted
            the ai2d letterbox, so the Teensy doesn't need to know the
            model input size.

Teensy → K230, 1 byte every K230_CMD_INTERVAL ms (unchanged):
- `0x00` = idle (K230 will push a "type=0xFF" packet every ~10 frames)
- `0x01` = run (K230 runs inference and pushes the single best detection)

## 1. `globals.h` — add a struct + extern

Replace the existing `Detection` struct block with:

```cpp
// --- K230D AI single-best detection (det_live_xy.py) ---
enum K230ObjectType : uint8_t {
    K230_NONE   = 0xFF,
    K230_BLACK  = 0,
    K230_SILVER = 1,
    // (extend if you ever switch to a different model)
};

struct K230Object {
    K230ObjectType type;    // K230_NONE if no detection
    uint8_t        score;   // 0..255, = round(conf * 255)
    int16_t        x;       // pixel center, sensor coords
    int16_t        y;
    uint32_t       lastUpdateMs;   // millis() of last packet
};

extern K230Object object;
extern bool       k230Running;     // True while we're sending 0x01
```

You can keep `Detection detections[K230_MAX_DETECTIONS]` and
`detectionCount` if other code still uses them, but they'll just be
ignored — nothing populates them under the new protocol.

## 2. `config.h` — no changes needed

`K230_BAUD`, `K230_CMD_INTERVAL` already exist.

## 3. `Comms.ino` — replace the parser + add storage

Find the block beginning `Detection detections[K230_MAX_DETECTIONS];`
and replace down through the end of `_parseK230()` with this:

```cpp
// --- K230D single-best detection storage (declared extern in globals.h) ---
K230Object object       = { K230_NONE, 0, 0, 0, 0 };
bool       k230Running  = false;

// ---------------------------------------------------------------------------
//  K230D 8-byte fixed-packet parser
//    [0xAA] [0x55] [type] [score] [x_hi] [x_lo] [y_hi] [y_lo]
// ---------------------------------------------------------------------------
namespace {
    enum K230ParseState : uint8_t {
        K230_WAIT_AA, K230_WAIT_55, K230_READ_BODY
    };
    K230ParseState _k_state = K230_WAIT_AA;
    uint8_t        _k_body[6];   // type, score, x_hi, x_lo, y_hi, y_lo
    uint8_t        _k_idx  = 0;
}

static void _parseK230() {
    while (Serial5.available()) {
        uint8_t b = (uint8_t)Serial5.read();
        switch (_k_state) {
            case K230_WAIT_AA:
                if (b == 0xAA) _k_state = K230_WAIT_55;
                break;
            case K230_WAIT_55:
                _k_state = (b == 0x55) ? K230_READ_BODY : K230_WAIT_AA;
                _k_idx = 0;
                break;
            case K230_READ_BODY:
                _k_body[_k_idx++] = b;
                if (_k_idx >= 6) {
                    object.type  = (K230ObjectType)_k_body[0];
                    object.score = _k_body[1];
                    object.x     = (int16_t)((uint16_t)_k_body[2] << 8 | _k_body[3]);
                    object.y     = (int16_t)((uint16_t)_k_body[4] << 8 | _k_body[5]);
                    object.lastUpdateMs = millis();
                    _k_state = K230_WAIT_AA;
                }
                break;
        }
    }
}
```

The rest of `Comms.ino` (the Teensy → K230 1-byte send block) stays
unchanged.

## 4. Choose when to switch K230 state

In `STS-imuMerge.ino`'s `updateComms()` call site, decide which states
should run the K230. Right now `Comms.ino` says
`k230Running = (robotState == EVACUATION_ZONE);`. To run detection
during, say, evacuation AND a future "object search" state:

```cpp
// inside updateComms() in Comms.ino, replace the assignment:
k230Running = (robotState == EVACUATION_ZONE ||
               robotState == OBJECT_SEARCH);   // add your states here
```

The 1-byte command is already sent every `K230_CMD_INTERVAL = 100 ms`,
so the K230 will see the state change within ~100 ms. The K230 begins
producing 8-byte packets at full frame rate once it receives `0x01`,
and switches to occasional `type=0xFF` heartbeats once it receives
`0x00`.

## 5. How to use `object` in your code

```cpp
// example: check if K230 found a black target in the last 200 ms
if (object.type == K230_BLACK &&
    millis() - object.lastUpdateMs < 200) {
    int cx = object.x;          // sensor pixel x
    int cy = object.y;          // sensor pixel y
    uint8_t conf = object.score; // 0..255
    // ... decide and act
}
```

`object.lastUpdateMs == 0` at boot, so the millis-since check will be
huge until the first packet arrives. The age check guards against stale
data when the K230 is paused.

## 6. Deploy `det_live_xy.py` on the K230D

Copy to the K230D SD card alongside the kmodel:

```
/data/k230-train/
├── model.kmodel             (from exports/v_float32 OR exports/q1p_qat_squant_kld_u8u8)
├── deploy_config.json       (matching the kmodel)
└── det_live_xy.py
```

Then on the K230D MicroPython REPL:

```python
import det_live_xy; det_live_xy.main()
```

To autorun on boot, save as `/data/main.py` (or `boot.py` on CanMV
v1.5-legacy — check your build) with the same body.

## Trade-off note on which kmodel to use

| kmodel | size | accuracy | ~per-frame on K230D (estimate) |
|---|---|---|---|
| `exports/v_float32/model.kmodel` | 3.98 MB | recall@0.4 = 0.987 | 1-5 s pure KPU |
| `exports/q1p_qat_squant_kld_u8u8/model.kmodel` | 1.35 MB | recall@0.4 = 0.184 | 0.1-0.5 s pure KPU |

If detection accuracy is critical and you can wait, use float32. If you
need real-time and can tolerate misses, use q1p_qat — but you'll need
to drop `CONF_THRESHOLD` in `det_live_xy.py` from `0.30` to about `0.05`
because the quantized model's scores are systematically lower.

## Important caveat about the histogram-equalized input

`det_live_xy.py` calls `img.histeq()` to match the way the training
images were captured (`cameraCapIMGv2.py`). If you ever retrain on
non-histeq'd images, **disable** `APPLY_HISTEQ` at the top of the file
or the model will see a different distribution and accuracy will drop.
