# K230D Zero / CanMV v1.5-legacy
#
# boot.py runs before main.py. We use it to wire sys.path so /sdcard/app
# and /sdcard/customLib are importable without prefixing every import.

import sys

for p in ("/sdcard/app", "/sdcard/customLib"):
    if p not in sys.path:
        sys.path.insert(0, p)
