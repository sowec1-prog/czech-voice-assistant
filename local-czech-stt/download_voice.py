"""Download the selected local Piper voice model."""
from __future__ import annotations

from pathlib import Path
from urllib.request import urlretrieve

VOICE = "sk_SK-lili-medium"
BASE_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/main/sk/sk_SK/lili/medium"
TARGET_DIR = Path(__file__).resolve().parent / "voices"

TARGET_DIR.mkdir(exist_ok=True)
for suffix in (".onnx", ".onnx.json"):
    destination = TARGET_DIR / f"{VOICE}{suffix}"
    if destination.exists() and destination.stat().st_size > 1000:
        print(f"Exists: {destination.name}")
        continue
    print(f"Downloading: {destination.name}")
    urlretrieve(f"{BASE_URL}/{VOICE}{suffix}?download=true", destination)

print("Voice ready.")
