#!/usr/bin/env bash
# Bash equivalent of the Makefile. Use on Linux/Mac/Git-Bash where you
# don't want to install make. Same commands as run.ps1.
#
# Usage: ./run.sh <target>   (see ./run.sh help)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DC="docker compose -f $HERE/../docker/docker-compose.yml"

train_py()  { $DC run --rm train python "$@"; }
nncase_py() {
  $DC run --rm nncase bash -c \
    "pip install -q opencv-python-headless pyyaml >/dev/null 2>&1; \
     export PATH=/usr/local/lib/python3.10/dist-packages:\$PATH; \
     cd /pipeline && python $*"
}

case "${1:-help}" in
  help)
    cat <<EOF
K230 pipeline runner (bash)

  build             Build both Docker images
  smoke             Stage 01 smoke-train (1 epoch)
  train             Stage 01 full train
  export            Stage 02 .pt -> .onnx
  calibrate         Stage 03 pick N calibration images
  compile           Stage 04 .onnx -> .kmodel (all variants)
  eval              Stage 05 PC eval each variant
  select            Stage 06 pick winner
  package           Stage 07 emit SD-card bundle
  all               Full pipeline (train -> ... -> package)
  all-skip-train    Pipeline without training (uses existing best.pt)
  clean             Wipe artifacts/

Examples:
  ./run.sh all-skip-train
  ./run.sh calibrate && ./run.sh compile && ./run.sh eval
EOF
    ;;
  build)          $DC build ;;
  smoke)          train_py stages/01_train.py --smoke ;;
  train)          train_py stages/01_train.py ;;
  export)         train_py stages/02_export.py ;;
  calibrate)      nncase_py stages/03_calibrate.py ;;
  compile)        nncase_py stages/04_compile.py ;;
  eval)           nncase_py stages/05_eval.py ;;
  select)         nncase_py stages/06_select.py ;;
  package)        nncase_py stages/07_package.py ;;
  all)
    "$0" train; "$0" export; "$0" calibrate; "$0" compile
    "$0" eval;  "$0" select; "$0" package
    echo "DONE -> artifacts/deploy/"
    ;;
  all-skip-train)
    "$0" export; "$0" calibrate; "$0" compile
    "$0" eval;   "$0" select;    "$0" package
    echo "DONE -> artifacts/deploy/  (used existing best.pt)"
    ;;
  clean)
    rm -rf "$HERE/artifacts/exports" "$HERE/artifacts/reports" \
           "$HERE/artifacts/deploy"  "$HERE/artifacts/calib" \
           "$HERE/artifacts/best.onnx"
    echo "Cleaned artifacts/"
    ;;
  *)
    echo "Unknown target: $1"; "$0" help; exit 2 ;;
esac
