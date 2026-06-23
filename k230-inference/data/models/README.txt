/data/models/ -- kmodel + matching deploy config + optional labels

Put these files here:

    <model>.kmodel        the YOLOv8 anchor-free kmodel

    deploy_config.json    matching deploy config. Set "kmodel_path" to the
                          kmodel filename (no leading slash) so the loader
                          resolves it relative to this directory.

    labels.txt            optional. One label per line. If present, it overrides
                          deploy_config.json categories.

Minimum deploy_config.json:

    {
      "kmodel_path":      "best.kmodel",
      "model_type":       "AnchorFreeDet",
      "img_size":         [640, 480],
      "num_classes":      3,
      "categories":       ["Dead", "Live", "Point"],
      "confidence_threshold": 0.3,
      "nms_threshold":    0.45,
      "preprocess": {
        "resize":         "direct (no letterbox)"
      }
    }

The live app reads kmodel_path, labels/categories, num_classes, img_size,
confidence_threshold, nms_threshold, and preprocess.resize. To update models,
copy the deploy folder's .kmodel, deploy_config.json, and labels.txt here,
then copy data/ to /data and sdcard/ to /sdcard on the K230.
