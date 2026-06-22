Drop your downloaded annotation export folder HERE.

The export folder is the one that contains:
    images/        labels/        classes.txt

You can drop either:
  - the export folder itself  (raw_export/project-2-at-2026-.../images, labels, ...)
  - or its contents directly  (raw_export/images, raw_export/labels, ...)

Then from k230-final-train/ run:
    py -3 k230_pipeline.py organize --out datasets/<your_name>

organize auto-finds the export in here — no long Downloads path to type.
If you keep several exports in here at once, pass --src to pick one.
