from __future__ import annotations

import json
from pathlib import Path
from typing import Protocol

import cv2
import numpy as np

from .cache import digest_file, identity
from .devices import resolve_device

PRESETS = {"Preview": 280, "Balanced": 518, "High": 756, "Maximum": 1036}


def create_provider(manifest_path, quality, device):
    contract = json.loads(Path(manifest_path).read_text()).get("contract")
    if contract == "video-depth-anything-stream-v1":
        from .video_inference import VideoDepth
        return VideoDepth(manifest_path, quality, device)
    if contract == "da3-distance-v1":
        from .da3_inference import DA3Depth
        return DA3Depth(manifest_path, quality, device)
    return OnnxDepth(manifest_path, quality, device)


class DepthProvider(Protocol):
    key: str
    def infer(self, rgb: np.ndarray) -> np.ndarray: ...


class OnnxDepth:
    """Explicit DA-V2-style NCHW RGB contract; models are never auto-downloaded."""
    def __init__(self, manifest_path: str, quality: str, device="auto"):
        import onnxruntime as ort

        path = Path(manifest_path).resolve(strict=True)
        manifest = json.loads(path.read_text())
        if manifest.get("contract") != "relative-inverse-depth-nchw-v1":
            raise ValueError("Unsupported ONNX model contract")
        model = (path.parent / manifest["file"].replace(chr(92), "/")).resolve(strict=True)
        checksum = digest_file(model)
        if checksum != manifest["sha256"]:
            raise ValueError("Model checksum does not match its manifest")
        device = resolve_device(device, backend="onnx")
        self.device = device
        if quality not in PRESETS:
            raise ValueError("Invalid model quality/device")
        self.size = PRESETS[quality]
        self.mean = np.asarray(manifest.get("mean", [.485, .456, .406]), dtype=np.float32)
        self.std = np.asarray(manifest.get("std", [.229, .224, .225]), dtype=np.float32)
        if self.mean.shape != (3,) or self.std.shape != (3,) or not np.isfinite(self.mean).all() or not np.isfinite(self.std).all() or (self.std <= 0).any():
            raise ValueError("Invalid model normalization")
        provider = {"cuda": "CUDAExecutionProvider", "cpu": "CPUExecutionProvider",
                    "coreml": "CoreMLExecutionProvider"}[device]
        if device == "cuda" and hasattr(ort, "preload_dlls"):
            ort.preload_dlls()
        if provider not in ort.get_available_providers():
            raise RuntimeError(f"{provider} unavailable; install a matching ONNX Runtime and CUDA/cuDNN runtime")
        options = ort.SessionOptions()
        options.intra_op_num_threads = 2
        optimization = manifest.get("graph_optimization", "all")
        if optimization not in ("all", "basic", "disabled"):
            raise ValueError("Invalid graph optimization setting")
        options.graph_optimization_level = {
            "all": ort.GraphOptimizationLevel.ORT_ENABLE_ALL,
            "basic": ort.GraphOptimizationLevel.ORT_ENABLE_BASIC,
            "disabled": ort.GraphOptimizationLevel.ORT_DISABLE_ALL,
        }[optimization]
        cuda_options = {"cudnn_conv_algo_search": "HEURISTIC", "cudnn_conv_use_max_workspace": "0"}
        self.session = ort.InferenceSession(str(model), sess_options=options,
                                           providers=[(provider, cuda_options)] if device == "cuda" else [provider])
        if self.session.get_providers()[0] != provider:
            raise RuntimeError("Requested inference device failed; refusing silent CPU fallback")
        inputs = self.session.get_inputs()
        if len(inputs) != 1 or len(inputs[0].shape) != 4 or inputs[0].type != "tensor(float)":
            raise ValueError("Model must have one float32 NCHW input")
        shape = inputs[0].shape
        if isinstance(shape[0], int) and shape[0] != 1 or isinstance(shape[1], int) and shape[1] != 3:
            raise ValueError("Model input must be 1x3xHxW")
        if any(isinstance(v, int) for v in shape[2:]):
            raise ValueError("Model spatial axes must be dynamic; use the included export tool")
        self.input_name = inputs[0].name
        self.output_name = manifest.get("output", self.session.get_outputs()[0].name)
        self.key = identity({"model": checksum, "manifest": manifest, "quality": quality,
                             "preprocess": "letterbox14-v1", "device": device, "ort": ort.__version__,
                             "cuda_options": cuda_options if device == "cuda" else None})

    def infer(self, rgb: np.ndarray) -> np.ndarray:
        h, w = rgb.shape[:2]
        # Preserve aspect ratio, cap the long side to the chosen preset, pad to patch multiple 14.
        scale = self.size / max(h, w)
        rh, rw = max(1, round(h * scale)), max(1, round(w * scale))
        image = cv2.resize(rgb, (rw, rh), interpolation=cv2.INTER_CUBIC).astype(np.float32) / 255
        ph, pw = (-rh) % 14, (-rw) % 14
        image = np.pad(image, ((0, ph), (0, pw), (0, 0)), mode="edge")
        tensor = np.ascontiguousarray(((image - self.mean) / self.std).transpose(2, 0, 1)[None])
        result = self.session.run([self.output_name], {self.input_name: tensor})[0]
        if result.ndim == 4 and result.shape[:2] == (1, 1):
            result = result[0, 0]
        elif result.ndim == 3 and result.shape[0] == 1:
            result = result[0]
        else:
            raise ValueError("Depth output must be 1xHxW or 1x1xHxW")
        if not np.isfinite(result).all():
            raise ValueError("Inference returned non-finite depth")
        result = cv2.resize(result, (rw + pw, rh + ph), interpolation=cv2.INTER_LINEAR)[:rh, :rw]
        return cv2.resize(result, (w, h), interpolation=cv2.INTER_LINEAR).astype(np.float32)
