# Environment setup

## Option A — Standalone venv (recommended for clean experiments)

```powershell
cd C:\Users\magic\Documents\robocup\RoboCupJunior_RescueLine\k230\train
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Verify:

```powershell
python -c "import nncase; print('nncase', nncase.__version__ if hasattr(nncase,'__version__') else 'OK')"
python -c "import torch; print('torch', torch.__version__)"
python -c "import onnx, onnxsim; print('onnx', onnx.__version__)"
```

## Option B — Reuse AI Cube's bundled environment

AI Cube ships a 2.3 GB Python environment that already has everything
matching what produced the broken kmodel. Use this if you want maximum
fidelity to AI Cube's tooling.

```powershell
$AICUBE = "C:\Users\magic\Downloads\k230model\AICube_V1.4_for_Windows\AICube_for_Windows\AICube"
# Add AICube's python + nncase to PATH for this shell session only
$env:PATH = "$AICUBE\python;$AICUBE\python\Scripts;$env:PATH"
$env:PYTHONPATH = "$AICUBE;$AICUBE\nncase;$env:PYTHONPATH"
python -c "import sys; print(sys.executable)"
python -c "import nncase; print('nncase OK')"
```

Caveat: AI Cube's `.exe` is Nuitka-compiled. The Python source for the
training/export pipeline is not exposed; we only get the bundled libraries.

## Option C — Existing system Python (what we have today)

The user's existing Python 3.10 install at
`C:\Users\magic\AppData\Local\Programs\Python\Python310` already has
`nncase==2.9.0` installed. This works as long as it isn't held back by
a torch version conflict.

```powershell
python -c "import nncase, sys; print(sys.executable); print(nncase)"
```

If the scripts here run, this is enough.

## nncase wheels

If `pip install nncase==2.9.0` fails:

```powershell
pip install --upgrade pip
pip install nncase==2.9.0 nncase-kpu==2.9.0 -f https://github.com/kendryte/nncase/releases
```

nncase is currently Windows + Python 3.7-3.10 only. Use Python 3.10.

## Calibration data prep

The matrix uses `data\calibration\` (16 images). Copy them manually from
`..\k230-training\datasets\my_dataset\train\images\` into this folder.
Mixing both classes (black + silver) and a few hard-negative backgrounds
gives the best PTQ result.
