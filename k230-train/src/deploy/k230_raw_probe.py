# K230D Zero side of the PC<->K230 raw-tensor verification.
#
# Reads `/data/k230-train/probe_input.bin` (a uint8 NCHW [1,3,480,640] tensor
# produced by `src/probe/pc_raw_probe.py`), feeds it directly to KPU, and
# saves the three KPU output tensors as `/data/k230-train/k230_out{0,1,2}.npy`.
#
# Then run `src/eval/compare_pc_k230.py` on the host to diff PC vs K230.
#
# Based on the verified-working k230-inference/CubaAI_img_camman.py raw mode.

import os
import gc
import time

import nncase_runtime as nn
import ulab.numpy as np
import aicube


ROOT_PATH    = "/data/k230-train"
KMODEL_PATH  = ROOT_PATH + "/model.kmodel"
INPUT_BIN    = ROOT_PATH + "/probe_input.bin"

OUT_PREFIX   = "k230_out"
SAVE_DIR     = ROOT_PATH
MODEL_W      = 640
MODEL_H      = 480

STRIDES      = [8, 16, 32]
NUM_CLASSES  = 2
CONF_THR     = 0.01
NMS_THR      = 0.5
ANCHORS_FLAT = [33, 43, 41, 56, 55, 63,
                59, 81, 72, 92, 87, 111,
                103, 134, 161, 121, 134, 157]


def load_raw_tensor(path):
    expected = 1 * 3 * MODEL_H * MODEL_W
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) != expected:
        raise ValueError(
            "RAW input size mismatch: have {} bytes, expected {}".format(
                len(raw), expected,
            )
        )
    arr = np.frombuffer(raw, dtype=np.uint8)
    return arr.reshape((1, 3, MODEL_H, MODEL_W))


def save_npy_minimal(arr, path):
    """Write a v1.0 .npy. ulab arrays don't have np.save, so build header by hand."""
    # ulab supports the numpy header format via tobytes for the array itself.
    # We'll write a fixed-format NumPy v1.0 .npy.
    shape = tuple(int(s) for s in arr.shape)
    dtype = arr.dtype
    # ulab dtype strings: 'uint8', 'int8', 'int16', 'uint16', 'int32', 'float'
    # Map to numpy descr strings.
    if dtype == np.uint8: descr = "|u1"
    elif dtype == np.int8: descr = "|i1"
    elif dtype == np.int16: descr = "<i2"
    elif dtype == np.uint16: descr = "<u2"
    elif dtype == np.int32: descr = "<i4"
    elif dtype == np.uint32: descr = "<u4"
    elif dtype == np.float: descr = "<f4"   # ulab's float is float32 by default on K230
    else: descr = "<f4"

    header = "{{'descr': '{0}', 'fortran_order': False, 'shape': {1}, }}".format(
        descr, shape,
    )
    # pad to 64-byte alignment after the 10-byte magic+ver+len prefix.
    base_len = 10 + len(header) + 1   # +1 for trailing newline
    pad = (64 - base_len % 64) % 64
    header_full = header + " " * pad + "\n"
    header_bytes = header_full.encode("latin-1")
    with open(path, "wb") as f:
        f.write(b"\x93NUMPY")
        f.write(b"\x01\x00")
        f.write(bytes([len(header_bytes) & 0xFF, (len(header_bytes) >> 8) & 0xFF]))
        f.write(header_bytes)
        f.write(arr.tobytes())


def stats(arr, label=""):
    flat = arr.reshape((-1,))
    n = min(2000, flat.shape[0])
    sample = flat[:n]
    mn = float(sample[0]); mx = float(sample[0]); s = 0.0
    for i in range(n):
        v = float(sample[i])
        if v < mn: mn = v
        if v > mx: mx = v
        s += v
    print("  {} shape={} sample_min={:.4g} sample_max={:.4g} sample_mean={:.4g}".format(
        label, arr.shape, mn, mx, s / n,
    ))


def main():
    print("===== k230_raw_probe =====")
    print("kmodel:", KMODEL_PATH)
    print("input :", INPUT_BIN)

    inp = load_raw_tensor(INPUT_BIN)
    stats(inp, "raw input")
    print("  first 20 values:", [int(v) for v in inp.reshape((-1,))[:20]])

    kpu = nn.kpu()
    kpu.load_kmodel(KMODEL_PATH)
    in_tensor = nn.from_numpy(inp)
    kpu.set_input_tensor(0, in_tensor)
    t0 = time.time_ns()
    kpu.run()
    t = (time.time_ns() - t0) / 1e6
    print("KPU run took {:.2f} ms".format(t))

    n = kpu.outputs_size()
    print("Output count:", n)
    results = []
    for i in range(n):
        data = kpu.get_output_tensor(i)
        arr = data.to_numpy()
        stats(arr, "out[{}]".format(i))
        # Save as numpy .npy so PC can load directly.
        npy_path = SAVE_DIR + "/" + OUT_PREFIX + str(i) + ".npy"
        save_npy_minimal(arr, npy_path)
        print("    saved ->", npy_path)
        # Keep a flat copy for aicube postprocess.
        total = 1
        for s in arr.shape:
            total *= s
        results.append(arr.reshape((total,)))
        del data
    del in_tensor

    # Run aicube postprocess too, for cross-check vs the PC decoder.
    print("\nAicube postprocess sanity:")
    img_size = [MODEL_W, MODEL_H]
    frame_size = [MODEL_W, MODEL_H]
    det = aicube.anchorbasedet_post_process(
        results[0], results[1], results[2],
        img_size, frame_size, STRIDES,
        NUM_CLASSES, CONF_THR, NMS_THR,
        ANCHORS_FLAT, False,
    )
    print("  detections (conf>={}): {}".format(CONF_THR, len(det) if det else 0))
    if det:
        try:
            det = sorted(det, key=lambda d: -d[1])
        except Exception:
            pass
        for d in det[:10]:
            print("    cls={} score={:.4f} box=({:.1f},{:.1f},{:.1f},{:.1f})".format(
                d[0], d[1], d[2], d[3], d[4], d[5],
            ))
    del results
    del kpu
    gc.collect()
    nn.shrink_memory_pool()
    print("===== end =====")


if __name__ == "__main__":
    nn.shrink_memory_pool()
    main()
