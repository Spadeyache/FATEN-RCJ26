/data/models/  â€” kmodel + matching deploy_config

Put these two files here:

    victim.kmodel         the YOLOv8 anchor-free kmodel (winning variant from
                          k230-train/exports/...)

    deploy_config.json    matching deploy config. Set "kmodel_path" to just
                          "victim.kmodel" (no leading slash) so the loader
                          resolves it relative to this directory.

Minimum deploy_config.json for a 2-class anchor-free model:

    {
      "kmodel_path":      "victim.kmodel",
      "model_type":       "AnchorFreeDet",
      "img_size":         [640, 480],
      "num_classes":      2,
      "categories":       ["silver", "black"]
    }

Other fields you may want to carry over from the train pipeline
(conf_threshold, nms_threshold, strides) are not read by main.py --
those live in /sdcard/app/config.py. Keep them in the JSON only as
documentation of what the kmodel was trained for.
