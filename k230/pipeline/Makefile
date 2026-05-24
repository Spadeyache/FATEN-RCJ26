.PHONY: help build train smoke export calibrate compile eval select package all clean

DC := docker compose -f ../docker/docker-compose.yml
TRAIN := $(DC) run --rm train python
NNCASE := $(DC) run --rm nncase bash -c 'pip install -q opencv-python-headless pyyaml >/dev/null 2>&1; export PATH=/usr/local/lib/python3.10/dist-packages:$$PATH; cd /pipeline &&

help:
	@echo "Targets:"
	@echo "  build      Build both Docker images"
	@echo "  smoke      Stage 01 smoke-train (1 epoch)"
	@echo "  train      Stage 01 full train"
	@echo "  export     Stage 02 .pt -> .onnx"
	@echo "  calibrate  Stage 03 pick N calibration images"
	@echo "  compile    Stage 04 .onnx -> .kmodel (all variants)"
	@echo "  eval       Stage 05 PC eval each variant"
	@echo "  select     Stage 06 pick winner"
	@echo "  package    Stage 07 emit SD-card bundle"
	@echo "  all        Full pipeline (train -> ... -> package)"
	@echo "  all-skip-train  Pipeline without training (uses existing best.pt)"

build:
	$(DC) build

smoke:
	$(TRAIN) stages/01_train.py --smoke

train:
	$(TRAIN) stages/01_train.py

export:
	$(TRAIN) stages/02_export.py

calibrate:
	$(NNCASE) python stages/03_calibrate.py'

compile:
	$(NNCASE) python stages/04_compile.py'

eval:
	$(NNCASE) python stages/05_eval.py'

select:
	$(NNCASE) python stages/06_select.py'

package:
	$(NNCASE) python stages/07_package.py'

all: train export calibrate compile eval select package
	@echo "DONE -> artifacts/deploy/"

all-skip-train: export calibrate compile eval select package
	@echo "DONE -> artifacts/deploy/  (used existing best.pt)"

clean:
	rm -rf artifacts/exports artifacts/reports artifacts/deploy artifacts/calib artifacts/best.onnx
