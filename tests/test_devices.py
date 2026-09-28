import sys
from types import SimpleNamespace

import pytest

from stereodepth_worker import devices


def fake_torch(monkeypatch, *, cuda=False, mps=False, capability=(8, 6), version="12.8"):
    module = SimpleNamespace(cuda=SimpleNamespace(is_available=lambda: cuda,
        get_device_capability=lambda: capability), version=SimpleNamespace(cuda=version),
        backends=SimpleNamespace(mps=SimpleNamespace(is_available=lambda: mps)))
    monkeypatch.setitem(sys.modules, "torch", module)


@pytest.mark.parametrize("capability", [(8, 6), (8, 9), (12, 0)])
def test_rtx_generations(monkeypatch, capability):
    fake_torch(monkeypatch, cuda=True, capability=capability)
    assert devices.resolve_device("auto", backend="torch") == "cuda"


def test_blackwell_rejects_old_cuda(monkeypatch):
    fake_torch(monkeypatch, cuda=True, capability=(12, 0), version="12.6")
    with pytest.raises(RuntimeError, match="12.8"):
        devices.resolve_device("cuda", backend="torch")


@pytest.mark.parametrize("cuda,mps,expected", [(False, True, "mps"), (False, False, "cpu"), (True, True, "cuda")])
def test_torch_automatic_selection(monkeypatch, cuda, mps, expected):
    fake_torch(monkeypatch, cuda=cuda, mps=mps)
    assert devices.resolve_device("auto", backend="torch") == expected


@pytest.mark.parametrize("device", ["cuda", "mps"])
def test_explicit_torch_backend_never_falls_back(monkeypatch, device):
    fake_torch(monkeypatch)
    with pytest.raises(RuntimeError):
        devices.resolve_device(device, backend="torch")


def test_coreml_is_not_a_torch_device(monkeypatch):
    fake_torch(monkeypatch)
    with pytest.raises(ValueError, match="only for ONNX"):
        devices.resolve_device("coreml", backend="torch")


@pytest.mark.parametrize("requested,system,providers,expected", [
    ("auto", "Darwin", ["CoreMLExecutionProvider", "CPUExecutionProvider"], "coreml"),
    ("mps", "Darwin", ["CoreMLExecutionProvider", "CPUExecutionProvider"], "coreml"),
    ("auto", "Windows", ["CUDAExecutionProvider", "CPUExecutionProvider"], "cuda"),
    ("auto", "Darwin", ["CPUExecutionProvider"], "cpu"),
    ("cpu", "Windows", ["CUDAExecutionProvider", "CPUExecutionProvider"], "cpu"),
])
def test_onnx_selection(monkeypatch, requested, system, providers, expected):
    monkeypatch.setitem(sys.modules, "onnxruntime", SimpleNamespace(get_available_providers=lambda: providers))
    monkeypatch.setattr(devices.platform, "system", lambda: system)
    assert devices.resolve_device(requested, backend="onnx") == expected


def test_explicit_onnx_backend_never_falls_back(monkeypatch):
    monkeypatch.setitem(sys.modules, "onnxruntime", SimpleNamespace(get_available_providers=lambda: ["CPUExecutionProvider"]))
    with pytest.raises(RuntimeError, match="CoreMLExecutionProvider"):
        devices.resolve_device("mps", backend="onnx")


def test_environment_and_cache_paths(monkeypatch):
    monkeypatch.setenv("S3D_DEVICE", "cpu")
    assert devices.requested_device() == "cpu"
    assert devices.requested_device("auto") == "auto"
    monkeypatch.setenv("S3D_DEVICE", "typo")
    with pytest.raises(ValueError):
        devices.requested_device()
    monkeypatch.delenv("S3D_CACHE_ROOT", raising=False)
    monkeypatch.setattr(devices.platform, "system", lambda: "Darwin")
    assert devices.default_cache_root() == devices.Path.home() / "Library/Caches/StereoDepth/cache"
    monkeypatch.setenv("S3D_CACHE_ROOT", "custom-cache")
    assert devices.default_cache_root() == devices.Path("custom-cache")
