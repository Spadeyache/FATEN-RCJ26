"""Stage 04: ONNX -> kmodel for each PTQ variant.

Runs inside `k230-nncase` container. Wraps train/src/convert/convert_kmodel.py.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, "/k230-train/src")

from shared.config import load

PTQ_CODE = {
    ("Kld", "uint8", "uint8"): 3,
    ("Kld", "uint8", "int16"): 4,
    ("Kld", "int16", "uint8"): 5,
}

def main():
    cfg = load()
    c = cfg["compile"]
    onnx = cfg["export"]["onnx"]
    calib_dir = cfg["calibrate"]["out_dir"]
    calib_count = cfg["calibrate"]["count"]
    iw = cfg["train"]["imgsz"][1]   # W = 640
    ih = cfg["train"]["imgsz"][0]   # H = 480

    from convert.convert_kmodel import compile_one

    exports = Path(c["exports_dir"])
    exports.mkdir(parents=True, exist_ok=True)
    results = []

    for v in c["variants"]:
        key = (c["calibrate_method"], v["quant_type"], v["w_quant_type"])
        ptq_code = PTQ_CODE[key]
        out_dir = exports / v["name"]
        print(f"\n[stage 04] compiling {v['name']} -> {out_dir}")
        summary = compile_one(
            onnx_path=onnx,
            output_dir=str(out_dir),
            calib_dir=calib_dir,
            input_width=iw,
            input_height=ih,
            calib_count=calib_count,
            ptq=ptq_code,
            preprocess_mode=c["preprocess_mode"],
            mean_imagenet=[0.485, 0.456, 0.406],
            std_imagenet=[0.229, 0.224, 0.225],
            swapRB=False,
            dump=False,
            finetune_weights=c["finetune_weights"],
            use_letterbox=c["use_letterbox"],
        )
        results.append((v["name"], summary))

    print("\n[stage 04] DONE:")
    for n, s in results:
        print(f"  {n}: {s['kmodel_size_bytes']/1e6:.2f} MB  ({s['compile_seconds']:.1f}s)")

if __name__ == "__main__":
    main()
