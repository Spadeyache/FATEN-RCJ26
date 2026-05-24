"""Shared pipeline config loader."""
from pathlib import Path
import yaml

CONFIG_PATH = Path(__file__).resolve().parent.parent / "configs" / "pipeline.yaml"

def load(path: Path = CONFIG_PATH) -> dict:
    with open(path) as f:
        return yaml.safe_load(f)
