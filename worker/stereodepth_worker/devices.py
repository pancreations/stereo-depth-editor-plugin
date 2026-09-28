"""Backend selection shared by interactive rendering and offline jobs."""
import os
import platform
from pathlib import Path

DEVICES = ("auto", "cpu", "cuda", "mps", "coreml")


def requested_device(device=None):
    value = device or os.environ.get("S3D_DEVICE", "auto")
    if value not in DEVICES:
        raise ValueError(f"Invalid device {value!r}; choose {', '.join(DEVICES)}")
    return value


def resolve_device(device=None, *, backend):
    device = requested_device(device)
    if backend == "onnx":
        import onnxruntime as ort
        available = ort.get_available_providers()
        if device == "mps":
            device = "coreml"  # ONNX uses CoreML; PyTorch uses Metal/MPS.
        if device == "auto":
            device = ("coreml" if platform.system() == "Darwin" and "CoreMLExecutionProvider" in available
                      else "cuda" if "CUDAExecutionProvider" in available else "cpu")
        provider = {"cpu": "CPUExecutionProvider", "cuda": "CUDAExecutionProvider",
                    "coreml": "CoreMLExecutionProvider"}[device]
        if provider not in available:
            raise RuntimeError(f"{provider} unavailable. Install the matching worker extra (cpu, cuda, or apple).")
        return device
    if backend != "torch":
        raise ValueError(f"Unknown inference backend: {backend}")
    import torch
    if device == "coreml":
        raise ValueError("CoreML is only for ONNX models; select mps or auto for PyTorch models")
    mps = getattr(torch.backends, "mps", None)
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "mps" if mps and mps.is_available() else "cpu"
    if device == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA PyTorch unavailable; install the cu128 build or select cpu")
        if torch.cuda.get_device_capability()[0] >= 12:
            version = tuple(int(n) for n in (torch.version.cuda or "0.0").split(".")[:2])
            if version < (12, 8):
                raise RuntimeError("RTX 50 series requires PyTorch built with CUDA 12.8 or newer")
    if device == "mps" and not (mps and mps.is_available()):
        raise RuntimeError("MPS unavailable; use native arm64 Python on Apple silicon or select cpu")
    return device


def default_cache_root():
    override = os.environ.get("S3D_CACHE_ROOT")
    if override:
        return Path(override)
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Caches" / "StereoDepth" / "cache"
    return Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "StereoDepth" / "cache"
