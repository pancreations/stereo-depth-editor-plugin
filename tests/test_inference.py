import json

import numpy as np
import onnx
from onnx import TensorProto, helper
import pytest

from stereodepth_worker.cache import digest_file
from stereodepth_worker.inference import OnnxDepth


def make_model(tmp_path):
    # Tiny real ONNX graph validates runtime I/O and preprocessing, not model quality.
    model_path = tmp_path / "test.onnx"
    graph = helper.make_graph([
        helper.make_node("ReduceMean", ["image"], ["depth"], axes=[1], keepdims=0)
    ], "depth-contract-test", [helper.make_tensor_value_info("image", TensorProto.FLOAT, [1, 3, "h", "w"])],
        [helper.make_tensor_value_info("depth", TensorProto.FLOAT, [1, "h", "w"])])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 9
    onnx.save(model, model_path)
    manifest = tmp_path / "model.json"
    manifest.write_text(json.dumps(dict(file=model_path.name, sha256=digest_file(model_path),
                                       contract="relative-inverse-depth-nchw-v1", mean=[0, 0, 0], std=[1, 1, 1])))
    return manifest


def test_real_onnx_runtime_contract_and_aspect_ratio(tmp_path):
    provider = OnnxDepth(str(make_model(tmp_path)), "Preview", "cpu")
    image = np.full((19, 71, 3), 128, dtype=np.uint8)
    depth = provider.infer(image)
    assert depth.shape == (19, 71)
    np.testing.assert_allclose(depth, 128 / 255, atol=1e-6)


def test_model_checksum(tmp_path):
    manifest = make_model(tmp_path)
    (tmp_path / "test.onnx").write_bytes(b"corrupt")
    with pytest.raises(ValueError, match="checksum"):
        OnnxDepth(str(manifest), "Preview", "cpu")


def test_accelerator_session_fallback_is_rejected(tmp_path, monkeypatch):
    import onnxruntime as ort
    original = ort.InferenceSession
    monkeypatch.setattr(ort, "get_available_providers", lambda: ["CoreMLExecutionProvider", "CPUExecutionProvider"])
    monkeypatch.setattr(ort, "InferenceSession", lambda path, **kwargs: original(path, providers=["CPUExecutionProvider"]))
    with pytest.raises(RuntimeError, match="refusing silent CPU fallback"):
        OnnxDepth(str(make_model(tmp_path)), "Preview", "mps")
