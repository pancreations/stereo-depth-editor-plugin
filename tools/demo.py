"""Create a reproducible end-to-end test scene and render both SBS formats."""
import argparse
from pathlib import Path
import subprocess
import time

import numpy as np
from PIL import Image

from stereodepth_worker.cache import Cache
from stereodepth_worker.jobs import Jobs


def pfm_write(path, data):
    data = np.asarray(data, dtype="<f4")
    with path.open("wb") as f:
        f.write(f"{'PF' if data.ndim == 3 else 'Pf'}\n{data.shape[1]} {data.shape[0]}\n-1.0\n".encode())
        f.write(np.flipud(data).tobytes())


def pfm_read(path):
    with path.open("rb") as f:
        channels = 3 if f.readline().strip() == b"PF" else 1
        w, h = map(int, f.readline().split()); f.readline()
        return np.flipud(np.frombuffer(f.read(), dtype="<f4").reshape(h, w, channels))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", type=Path, default=Path("build/Release/s3d_render.exe"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/demo"))
    parser.add_argument("--model", type=Path, help="Use a registered ONNX, VDA or DA3 model instead of external depth")
    parser.add_argument("--device", choices=["auto", "cpu", "cuda", "mps", "coreml"], default="cpu")
    parser.add_argument("--quality", choices=["Preview", "Balanced", "High", "Maximum"], default="Preview")
    args = parser.parse_args()
    color_dir = args.output / "source"; depth_dir = args.output / "depth"
    color_dir.mkdir(parents=True, exist_ok=True); depth_dir.mkdir(parents=True, exist_ok=True)
    h, w = 180, 320
    yy, xx = np.mgrid[:h, :w]
    for i in range(4):
        near = (xx > 110 + i * 4) & (xx < 190 + i * 4) & (yy > 36) & (yy < 146)
        color = np.empty((h, w, 3), dtype=np.float32)
        color[..., 0] = .08 + xx / w * .15
        color[..., 1] = .18 + yy / h * .25
        color[..., 2] = .4 + ((xx // 16 + yy // 16) % 2) * .15
        color[near] = [.95, .48, .12]
        color[near & ((xx % 12) < 3)] = [1, .72, .36]
        depth = np.where(near, .9, .15).astype(np.float32)
        Image.fromarray(np.uint8(color * 255)).save(color_dir / f"frame{i:04d}.png")
        Image.fromarray(np.uint16(depth * 65535)).save(depth_dir / f"depth{i:04d}.png")
        if i == 0:
            pfm_write(args.output / "source.pfm", color)
    jobs = Jobs(Cache(args.output / "cache"))
    request = dict(source=str(color_dir / "frame0000.png"), depth_override=args.model is None,
                   depth_source=str(depth_dir / "depth0000.png"), fps="24000/1001")
    if args.model:
        request.update(model=str(args.model), quality=args.quality, device=args.device, temporal={"enabled": True})
    job = jobs.submit(request)
    deadline = time.monotonic() + 120
    while jobs.active is not None:
        if time.monotonic() > deadline:
            raise RuntimeError("Demo job timed out")
        time.sleep(.05)
    status = jobs.status(job)
    if status["state"] != "complete":
        raise RuntimeError(status)
    for mode in ("half", "full"):
        output = args.output / f"{mode}-sbs.pfm"
        command = [str(args.renderer.resolve()), str(args.output / "source.pfm"),
                   str(jobs.cache.frame(status["key"], 0)), str(output), mode, "2", ".5"]
        if args.model:
            metadata = jobs.cache.metadata(status["key"])
            command.extend([str(metadata["low"]), str(metadata["high"])])
        subprocess.run(command, check=True)
        rendered = pfm_read(output)
        assert rendered.shape == (h, w * (2 if mode == "full" else 1), 3)
        assert np.isfinite(rendered).all()
        Image.fromarray(np.uint8(np.clip(rendered, 0, 1) * 255)).save(output.with_suffix(".png"))
    print(f"{'AI + temporal' if args.model else 'External PNG sequence'} -> float cache -> C++ stereo: passed. Artifacts: {args.output.resolve()}")


if __name__ == "__main__":
    main()
