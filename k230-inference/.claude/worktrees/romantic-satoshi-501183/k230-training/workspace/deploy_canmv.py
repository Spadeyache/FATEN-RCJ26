from libs.YOLO import YOLOv8
from libs.Utils import read_image
import gc

if __name__ == "__main__":
    # 1. パラメータの設定
    img_path = "/sdcard/devKmodels/victim/victim_0110.jpg"
    kmodel_path = "/sdcard/devKmodels/best_640x480_k230_28-2.kmodel"
    labels = ["black", "silver"]
    model_input_size=[640,480]

    # 2. 画像の読み込みと変換
    # read_image は HWC(Height, Width, Channel) から CHW への変換も行います [1]
    img_chw, img_rgb888 = read_image(img_path)

    # 推論に使用する画像の解像度を取得 [width, height]
    rgb888p_size = [img_chw.shape[2], img_chw.shape[1]]  # [W, H]

    # debug
#    print("img_chw shape:", img_chw.shape)   # (C, H, W)
#    print("img_rgb888 size:", img_rgb888.width(), img_rgb888.height())


    # 3. YOLOv8 インスタンスの初期化
    # mode="image" を指定することで画像ファイルモードになります [4]
    yolo = YOLOv8(
        task_type="detect",
        mode="image",
        kmodel_path=kmodel_path,
        labels=labels,
        rgb888p_size=rgb888p_size,
        model_input_size=model_input_size,
        conf_thresh=0.05,
        nms_thresh=0.45
    )

    # 4. 前処理の設定と推論の実行
    # 内部的に Ai2d を使用してリサイズ等の前処理が構成されます [5, 6]
    yolo.config_preprocess()
    res = yolo.run(img_chw)

    # 5. 結果の表示と描画
    print("Inference Result:", res)
    yolo.draw_result(res, img_rgb888)

    # 6. 後片付け
    yolo.deinit()
    gc.collect()
