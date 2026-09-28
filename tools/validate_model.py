"""Run the exported real depth model through all presets and record CPU timings."""
import argparse
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image

from stereodepth_worker.inference import OnnxDepth, PRESETS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/model-validation"))
    parser.add_argument("--device", choices=["auto", "cpu", "cuda", "mps", "coreml"], default="cpu")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with Image.open(args.image) as source:
        image = np.asarray(source.convert("RGB"))
    measurements = []
    for quality in PRESETS:
        model = OnnxDepth(str(args.model), quality, args.device)
        start = time.perf_counter()
        depth = model.infer(image)
        elapsed = time.perf_counter() - start
        assert depth.shape == image.shape[:2] and np.isfinite(depth).all()
        low, high = np.percentile(depth, [.5, 99.5])
        preview = np.uint16(np.clip((depth - low) / max(high - low, 1e-6), 0, 1) * 65535)
        Image.fromarray(preview).save(args.output / f"{quality.lower()}-depth.png")
        measurements.append(dict(quality=quality, width=image.shape[1], height=image.shape[0],
                                 device=args.device, seconds=elapsed, minimum=float(depth.min()), maximum=float(depth.max())))
        print(f"{quality}: {elapsed:.3f}s, output {depth.shape}", flush=True)
    (args.output / "results.json").write_text(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    main()
