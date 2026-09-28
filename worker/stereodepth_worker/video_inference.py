"""Optional official Video Depth Anything streaming provider.

Requires a trusted local upstream checkout and checkpoint; no downloads occur here.
Stateful providers must replay the whole ordered source when a cache is partial.
"""
import importlib
import json
from pathlib import Path
import sys

import cv2
import numpy as np

from .cache import digest_file, identity
from .inference import PRESETS
from .devices import resolve_device


class VideoDepth:
    stateful = True

    def __init__(self, manifest_path, quality, device="auto"):
        import torch
        path = Path(manifest_path).resolve(strict=True)
        manifest = json.loads(path.read_text())
        configurations = {"vits": (64, [48, 96, 192, 384]), "vitb": (128, [96, 192, 384, 768]),
                          "vitl": (256, [256, 512, 1024, 1024])}
        encoder = manifest.get("encoder")
        if manifest.get("contract") != "video-depth-anything-stream-v1" or encoder not in configurations:
            raise ValueError("Unsupported video depth checkpoint architecture")
        model_path = (path.parent / manifest["file"].replace(chr(92), "/")).resolve(strict=True)
        if digest_file(model_path) != manifest["sha256"]:
            raise ValueError("Video model checksum does not match manifest")
        repo = (path.parent / manifest["repository"].replace(chr(92), "/")).resolve(strict=True)
        source_files = sorted(list((repo / "video_depth_anything").rglob("*.py")) + list((repo / "utils").rglob("*.py")))
        source_entries = {p.relative_to(repo).as_posix(): digest_file(p) for p in source_files}
        source_hashes = {identity(source_entries), identity({k.replace("/", chr(92)): v for k, v in source_entries.items()})}
        if not source_files or manifest["source_sha256"] not in source_hashes:
            raise ValueError("Video model source changed; regenerate and review the manifest")
        device = resolve_device(device, backend="torch")
        if quality not in PRESETS:
            raise ValueError("Invalid video quality/device")
        if device == "cuda" and not torch.cuda.is_available():
            raise RuntimeError("CUDA PyTorch is unavailable for video depth; choose CPU or install CUDA PyTorch")
        # The upstream project imports a generic 'utils' package. Reject a collision.
        for name, expected in (("video_depth_anything", repo / "video_depth_anything"), ("utils", repo / "utils")):
            loaded = sys.modules.get(name)
            if loaded and not any(Path(p).resolve() == expected for p in getattr(loaded, "__path__", [])):
                raise RuntimeError(f"Python module collision for {name}; run the video worker in a separate process")
        if str(repo) not in sys.path:
            sys.path.insert(0, str(repo))
        model_class = importlib.import_module("video_depth_anything.video_depth_stream").VideoDepthAnything
        features, channels = configurations[encoder]
        self.model = model_class(encoder=encoder, features=features, out_channels=channels)
        self.model.load_state_dict(torch.load(model_path, map_location="cpu", weights_only=True), strict=True)
        self.model.to(device).eval()
        self.device = device
        self.size = PRESETS[quality]
        self.previous_guide = None
        self.metric = manifest.get("metric", False)
        self.key = identity({"manifest": manifest, "quality": quality, "device": device, "torch": torch.__version__,
                             "algorithm": "vda-stream-pad14-cut55-v1"})

    def reset(self):
        self.model.transform = None
        self.model.frame_id_list = []
        self.model.frame_cache_list = []
        self.model.id = -1
        self.previous_guide = None

    def infer(self, rgb):
        h, w = rgb.shape[:2]
        guide = cv2.resize(cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY), (64, 36)).astype(np.float32)
        if self.previous_guide is not None and np.mean(np.abs(guide - self.previous_guide)) > 55:
            self.reset()
        self.previous_guide = guide
        factor = self.size / max(h, w)
        rh, rw = max(1, round(h * factor)), max(1, round(w * factor))
        resized = cv2.resize(rgb, (rw, rh), interpolation=cv2.INTER_CUBIC)
        # Bound VRAM, preserve arbitrary source aspect ratio, and avoid upstream's
        # extreme-aspect input_size reduction by padding the analysis tensor.
        floor_side = int(np.ceil(self.size / 1.75 / 14)) * 14
        ph = max(int(np.ceil(rh / 14)) * 14, floor_side)
        pw = max(int(np.ceil(rw / 14)) * 14, floor_side)
        padded = np.pad(resized, ((0, ph-rh), (0, pw-rw), (0, 0)), mode="edge")
        result = self.model.infer_video_depth_one(padded, input_size=min(ph, pw), device=self.device,
                                                  fp32=self.device != "cuda")
        if result.shape != (ph, pw) or not np.isfinite(result).all():
            raise ValueError("Video model returned invalid depth")
        result = cv2.resize(result[:rh, :rw], (w, h), interpolation=cv2.INTER_LINEAR).astype(np.float32)
        if self.metric:
            if (result <= 0).any():
                raise ValueError("Metric video depth contains non-positive distances")
            result = 1 / np.maximum(result, 1e-6)
        return result
