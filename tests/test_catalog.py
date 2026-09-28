import io
import json

import pytest

from stereodepth_worker.catalog import Models, catalog
from stereodepth_worker.cache import digest_file


def test_catalog_keeps_every_size_available_and_defaults_large(tmp_path):
    models = Models(tmp_path)
    listing = models.listing()
    rows = {row["id"]: row for row in listing["models"]}
    assert len(rows) == 17
    assert listing["default"] == "dav2-large"
    assert all("dav2-" + size in rows for size in ("small", "base", "large"))
    assert all("vda-" + size + suffix in rows for size in ("small", "base", "large") for suffix in ("", "-metric"))
    assert rows["da3-giant-1.1"]["downloadable"]
    assert rows["da3nested-giant-large-1.1"]["downloadable"]
    assert not any(row["installed"] for row in rows.values())
    (tmp_path / "depth-anything-v2-large-fp16.json").write_text(json.dumps({"file": "large.onnx"}))
    assert not next(r for r in models.listing()["models"] if r["id"] == "dav2-large")["installed"]
    (tmp_path / "large.onnx").write_bytes(b"present")
    assert next(r for r in models.listing()["models"] if r["id"] == "dav2-large")["installed"]


def test_model_download_rejects_arbitrary_paths_and_incomplete_payload(tmp_path, monkeypatch):
    models = Models(tmp_path)
    for invalid in ("../outside", "https://example.com/weights", None, [], "dav2-large"):
        with pytest.raises(ValueError):
            models.submit(invalid)
    class Partial(io.BytesIO):
        headers = {"Content-Length": "100"}
    monkeypatch.setattr("stereodepth_worker.catalog.urlopen", lambda *a, **k: Partial(b"partial"))
    destination = tmp_path / "weights.pth"
    destination.write_bytes(b"existing valid weights")
    with pytest.raises(ValueError, match="Incomplete"):
        models._file("unused", destination, "vda-large")
    assert destination.read_bytes() == b"existing valid weights"
    assert not list(tmp_path.glob("*.download"))


def test_official_download_registers_matching_manifest(tmp_path, monkeypatch):
    root = tmp_path / "models"
    package = tmp_path / "third_party" / "Depth-Anything-3" / "src" / "depth_anything_3"
    package.mkdir(parents=True)
    (package / "example.py").write_text("# trusted fixture")
    models = Models(root)
    class Complete(io.BytesIO):
        headers = {"Content-Length": "7"}
    urls = []
    def download(url, **kwargs):
        urls.append(url)
        return Complete(b"weights")
    monkeypatch.setattr("stereodepth_worker.catalog.urlopen", download)
    models._download(next(r for r in catalog() if r["id"] == "da3mono-large"))
    assert models.status()["state"] == "complete"
    manifest = json.loads((root / "da3mono-large.json").read_text())
    assert manifest["sha256"] == digest_file(root / manifest["file"])
    assert manifest["architecture"] == "da3mono-large"
    assert manifest["contract"] == "da3-distance-v1"
    assert urls == ["https://huggingface.co/depth-anything/DA3MONO-LARGE/resolve/main/model.safetensors"]
