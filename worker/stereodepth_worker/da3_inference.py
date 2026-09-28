"""DA3 depth-only provider using upstream model modules, without export/web apps.

Any-view models run in single-view mode here. Flow stabilization remains available;
this is not a claim to implement the separate DA3 chunk/SLAM streaming pipeline.
"""
import json
from pathlib import Path
import sys

import cv2
import numpy as np

from .cache import digest_file, identity
from .inference import PRESETS
from .devices import resolve_device


class DA3Depth:
    def __init__(self, manifest_path, quality, device="auto"):
        import torch
        from safetensors.torch import load_file
        path = Path(manifest_path).resolve(strict=True)
        manifest = json.loads(path.read_text())
        if manifest.get("contract") != "da3-distance-v1":
            raise ValueError("Unsupported DA3 manifest")
        weights = (path.parent / manifest["file"].replace(chr(92), "/")).resolve(strict=True)
        if digest_file(weights) != manifest["sha256"]:
            raise ValueError("DA3 checkpoint checksum mismatch")
        repo = (path.parent / manifest["repository"].replace(chr(92), "/")).resolve(strict=True)
        package = repo / "src" / "depth_anything_3"
        files = sorted(list(package.rglob("*.py")) + list(package.rglob("*.yaml")))
        code_hash = identity({str(p.relative_to(repo)).replace('\\', '/'): digest_file(p) for p in files})
        if not files or code_hash != manifest["source_sha256"]:
            raise ValueError("DA3 source checksum mismatch; review and re-register the model")
        device = resolve_device(device, backend="torch")
        if quality not in PRESETS:
            raise ValueError("Invalid DA3 device/quality")
        if device == "cuda" and not torch.cuda.is_available():
            raise RuntimeError("Install CUDA PyTorch or select CPU for DA3")
        loaded = sys.modules.get("depth_anything_3")
        if loaded and not any(Path(p).resolve() == package for p in getattr(loaded, "__path__", [])):
            raise RuntimeError("Another DA3 source is already loaded; restart the worker")
        if str(repo / "src") not in sys.path:
            sys.path.insert(0, str(repo / "src"))
        from depth_anything_3.cfg import create_object, load_config
        from depth_anything_3.registry import MODEL_REGISTRY
        name = manifest["architecture"]
        if name not in MODEL_REGISTRY:
            raise ValueError("This DA3 source does not contain the selected architecture")
        self.model = create_object(load_config(MODEL_REGISTRY[name]))
        state = load_file(str(weights), device="cpu")
        if all(k.startswith("model.") for k in state):
            state = {k.removeprefix("model."): v for k, v in state.items()}
        self.model.load_state_dict(state, strict=True)
        del state
        self.model.to(device).eval()
        self.device, self.size = device, PRESETS[quality]
        self.key = identity({"manifest": manifest, "quality": quality, "device": device, "torch": torch.__version__,
                             "preprocess": "single-view-letterbox14-inverse-distance-v1"})

    def infer(self, rgb):
        import torch
        h, w = rgb.shape[:2]
        factor = self.size / max(h, w)
        rh, rw = max(1, round(h * factor)), max(1, round(w * factor))
        image = cv2.resize(rgb, (rw, rh), interpolation=cv2.INTER_CUBIC).astype(np.float32) / 255
        ph, pw = (-rh) % 14, (-rw) % 14
        image = np.pad(image, ((0, ph), (0, pw), (0, 0)), mode="edge")
        image = (image - np.array([.485, .456, .406], np.float32)) / np.array([.229, .224, .225], np.float32)
        tensor = torch.from_numpy(np.ascontiguousarray(image.transpose(2, 0, 1)[None, None])).to(self.device)
        devices = [torch.cuda.current_device()] if self.device == "cuda" else []
        dtype = torch.bfloat16 if self.device == "cuda" and torch.cuda.is_bf16_supported() else torch.float16
        # Upstream sky quantile sampling uses randomness; stabilize that choice for cache reproducibility.
        with torch.random.fork_rng(devices=devices), torch.inference_mode():
            torch.manual_seed(7)
            with torch.autocast(device_type=self.device, dtype=dtype, enabled=self.device == "cuda"):
                prediction = self.model(tensor, infer_gs=False, use_ray_pose=False, ref_view_strategy="first")
            distance = prediction["depth"][0, 0].float().cpu().numpy()
        if distance.shape != (rh + ph, rw + pw) or not np.isfinite(distance).all() or (distance <= 0).any():
            raise ValueError("DA3 returned invalid/non-positive distance depth")
        # Stereo needs nearness, not distance. Do not independently min/max normalize each frame.
        nearness = 1 / np.maximum(distance[:rh, :rw], 1e-6)
        return cv2.resize(nearness, (w, h), interpolation=cv2.INTER_LINEAR).astype(np.float32)
