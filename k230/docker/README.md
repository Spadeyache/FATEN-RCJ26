# nncase K230 simulate — Docker

Run [`k230_simulate-EN.ipynb`](https://github.com/kendryte/nncase/blob/master/examples/user_guide/k230_simulate-EN.ipynb) in a container.

## First run

```powershell
# 1. Grab the notebook + bundled test.onnx / test.tflite / helpers
./fetch-example.ps1

# 2. Build the image and start JupyterLab (first build pulls .NET 7 + nncase, ~5-10 min)
docker compose up --build
```

Open <http://localhost:8888/lab/tree/k230_simulate-EN.ipynb>.

## Using the bundled ONNX model

In the notebook, set the ONNX branch:

```python
model_path  = "test.onnx"
dump_path   = "tmp_onnx"
model_type  = "onnx"
```

Run all cells. Output `tmp_onnx/test.kmodel` appears in this folder on the host.

## Later — your own ONNX

Drop `your_model.onnx` next to `test.onnx` and point the notebook at it. No rebuild needed (the folder is bind-mounted into the container).

## Stop / clean up

```powershell
docker compose down            # stop container
docker image rm k230-nncase    # nuke the image if you want to rebuild from scratch
```
