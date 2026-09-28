import json
import threading
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import pytest

from stereodepth_worker.server import make_server


def test_server_device_applies_to_jobs(tmp_path):
    server, jobs, _ = make_server(tmp_path, port=0, device="cpu")
    try:
        assert jobs.device == "cpu"
    finally:
        server.server_close()


def test_external_sequence_import_endpoint(tmp_path):
    import numpy as np
    from PIL import Image
    for frame in (1, 2):
        Image.fromarray(np.full((2, 3), frame*40, np.uint8)).save(tmp_path/f"depth{frame:04d}.png")
    server, _, connection = make_server(tmp_path, port=0)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    def post(endpoint, data):
        request = Request(connection["url"]+endpoint, data=json.dumps(data).encode(), headers={
            "Authorization": "Bearer "+connection["token"], "Content-Type": "application/json"})
        with urlopen(request, timeout=5) as response:
            return response.read()
    try:
        path = str(tmp_path/"depth0001.png")
        sequence = json.loads(post("/v1/render/register-external", {"path": path, "import_mode": "sequence"}))["id"]
        still = json.loads(post("/v1/render/register-external", {"path": path, "import_mode": "still"}))["id"]
        assert sequence != still
        assert post("/v1/render/external-name", {"id": sequence}).decode().startswith("Sequence (2 frames):")
        assert post("/v1/render/external-name", {"id": still}).decode().startswith("Still:")
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)


def test_preview_open_requires_auth_and_reports_missing_device(tmp_path, monkeypatch):
    called = []
    def open_window():
        called.append(True)
        return {"opened": True, "window": "StereoDepth SBS Preview"}
    monkeypatch.setattr("stereodepth_worker.server.open_preview", open_window)
    server, _, connection = make_server(tmp_path, port=0)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        endpoint = connection["url"] + "/v1/preview/open"
        headers = {"Content-Type": "application/json"}
        with pytest.raises(HTTPError) as exc:
            urlopen(Request(endpoint, data=b'{}', headers=headers), timeout=5)
        assert exc.value.code == 401 and not called
        headers["Authorization"] = "Bearer " + connection["token"]
        with urlopen(Request(endpoint, data=b'{}', headers=headers), timeout=5) as response:
            assert json.load(response)["opened"] is True
        assert called == [True]
        def unavailable():
            raise ValueError("Enable StereoDepth SBS Preview in Playback preferences")
        monkeypatch.setattr("stereodepth_worker.server.open_preview", unavailable)
        with pytest.raises(HTTPError) as exc:
            urlopen(Request(endpoint, data=b'{}', headers=headers), timeout=5)
        assert exc.value.code == 400
        assert "Playback preferences" in json.load(exc.value)["error"]
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)


def test_worker_auth_and_path_validation(tmp_path):
    server, jobs, connection = make_server(tmp_path, port=0)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        with pytest.raises(HTTPError) as exc:
            urlopen(connection["url"] + "/v1/status", timeout=5)
        assert exc.value.code == 401
        req = Request(connection["url"] + "/v1/status", headers={"Authorization": "Bearer " + connection["token"]})
        with urlopen(req, timeout=5) as response:
            data = json.load(response)
        assert data["cache_root"] == str(tmp_path.resolve())
        assert data["capabilities"]["ai_inpainting"] is False
        req = Request(connection["url"] + "/v1/models", headers={"Authorization": "Bearer " + connection["token"]})
        with urlopen(req, timeout=5) as response:
            models = json.load(response)
        assert models["default"] == "dav2-large"
        assert len(models["models"]) == 17
        req = Request(connection["url"] + "/v1/models/download", data=b'{"id":"../arbitrary"}',
                      headers={"Authorization": "Bearer " + connection["token"], "Content-Type": "application/json"})
        with pytest.raises(HTTPError) as exc:
            urlopen(req, timeout=5)
        assert exc.value.code == 400
        req = Request(connection["url"] + "/v1/cache/clear", data=b'{"key":"../outside"}',
                      headers={"Authorization": "Bearer " + connection["token"], "Content-Type": "application/json"})
        with pytest.raises(HTTPError) as exc:
            urlopen(req, timeout=5)
        assert exc.value.code == 400
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)
