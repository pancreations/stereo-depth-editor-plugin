"""Single-image shape/finite and CUDA-memory smoke check; not a sustained benchmark."""
import argparse
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image
import torch

from stereodepth_worker.inference import create_provider


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--image", type=Path, default=Path("third_party/Depth-Anything-V2/assets/examples/demo01.jpg"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/torch-validation"))
    parser.add_argument("--device", choices=["auto", "cpu", "cuda", "mps", "coreml"], default="cuda")
    parser.add_argument("--quality", choices=["Preview", "Balanced", "High", "Maximum"], default="Balanced")
    args = parser.parse_args()
    rgb = np.asarray(Image.open(args.image).convert("RGB"))
    provider = create_provider(args.model, args.quality, args.device)
    device = provider.device
    if device == "cuda":
        torch.cuda.reset_peak_memory_stats()
        torch.cuda.synchronize()
    elif device == "mps":
        torch.mps.synchronize()
    started = time.perf_counter()
    result = provider.infer(rgb)
    if device == "cuda":
        torch.cuda.synchronize()
    elif device == "mps":
        torch.mps.synchronize()
    elapsed = time.perf_counter() - started
    assert result.shape == rgb.shape[:2] and np.isfinite(result).all()
    record = {"model": str(args.model), "quality": args.quality, "torch": torch.__version__,
              "device": torch.cuda.get_device_name() if device == "cuda" else provider.device,
              "seconds": elapsed, "shape": list(result.shape),
              "peak_allocated_mib": torch.cuda.max_memory_allocated() / 1048576 if device == "cuda" else None}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "result.json").write_text(json.dumps(record, indent=2))
    low, high = np.percentile(result, [1, 99])
    Image.fromarray(np.uint16(np.clip((result-low)/max(high-low, 1e-6), 0, 1)*65535)).save(args.output / "depth.png")
    print(json.dumps(record))


if __name__ == "__main__":
    main()
