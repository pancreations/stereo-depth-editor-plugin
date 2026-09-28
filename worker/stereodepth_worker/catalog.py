"""Explicit model choice and on-demand downloads from a fixed official catalogue."""
from __future__ import annotations

import json
import os
from pathlib import Path
import threading
from urllib.request import urlopen
import uuid

from .cache import atomic_json, digest_file, identity


def catalog():
    rows = []
    for size in ("small", "base", "large"):
        rows.append(dict(id=f"dav2-{size}", name=f"Depth Anything V2 {size.title()} · ONNX FP16", family="onnx",
                         manifest=f"depth-anything-v2-{size}-fp16.json", license="Apache-2.0" if size == "small" else "CC-BY-NC-4.0"))
    rows.append(dict(id="dav2-small-fp32", name="Depth Anything V2 Small · ONNX FP32", family="onnx",
                     manifest="depth_anything_v2_vits.json", license="Apache-2.0"))
    for size, encoder in (("small", "vits"), ("base", "vitb"), ("large", "vitl")):
        for metric in (False, True):
            prefix = "metric_" if metric else ""
            rows.append(dict(id=f"vda-{size}" + ("-metric" if metric else ""),
                             name=f"Video Depth Anything {size.title()}" + (" · Metric" if metric else " · Relative"),
                             family="video", encoder=encoder, metric=metric,
                             manifest=f"{prefix}video_depth_anything_{encoder}.json",
                             file=f"{prefix}video_depth_anything_{encoder}.pth",
                             hub=f"depth-anything/{'Metric-' if metric else ''}Video-Depth-Anything-{size.title()}",
                             license="Apache-2.0" if size == "small" else "CC-BY-NC-4.0"))
    da3 = [
        ("DA3-SMALL", "da3-small", "DA3 Small · Any-view", "Apache-2.0"),
        ("DA3-BASE", "da3-base", "DA3 Base · Any-view", "Apache-2.0"),
        ("DA3-LARGE-1.1", "da3-large", "DA3 Large 1.1 · Any-view", "CC-BY-NC-4.0"),
        ("DA3-GIANT-1.1", "da3-giant", "DA3 Giant 1.1 · Any-view", "CC-BY-NC-4.0"),
        ("DA3MONO-LARGE", "da3mono-large", "DA3 Mono Large", "Apache-2.0"),
        ("DA3METRIC-LARGE", "da3metric-large", "DA3 Metric Large", "Apache-2.0"),
        ("DA3NESTED-GIANT-LARGE-1.1", "da3nested-giant-large", "DA3 Nested Giant + Large 1.1", "CC-BY-NC-4.0"),
    ]
    for hub, architecture, name, license_name in da3:
        rows.append(dict(id=hub.lower(), name=name, family="da3", architecture=architecture,
                         manifest=hub.lower() + ".json", file=hub.lower() + "/model.safetensors",
                         hub="depth-anything/" + hub, license=license_name))
    return rows


class Models:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.state = {"state": "idle"}

    def listing(self):
        result = []
        for row in catalog():
            manifest = self.root / row["manifest"]
            ready = False
            if manifest.is_file():
                try:
                    data = json.loads(manifest.read_text())
                    ready = (manifest.parent / data["file"]).is_file()
                except (ValueError, KeyError, OSError):
                    pass
            tested = row["family"] == "onnx" or row["id"] in ("vda-small", "da3mono-large")
            validation = "CPU smoke-tested; RTX pending" if tested else "Adapter available; checkpoint validation pending"
            if row["id"] in ("da3mono-large", "dav2-large"):
                validation = "CPU + RTX 5070 Ti smoke-tested"
            result.append({**row, "installed": ready, "downloadable": "hub" in row, "path": str(manifest),
                           "validation": validation})
        return {"models": result, "download": self.status(), "default": "dav2-large"}

    def status(self):
        with self.lock:
            return dict(self.state)

    def _set(self, **state):
        with self.lock:
            self.state = state

    def submit(self, model_id):
        choices = {row["id"]: row for row in catalog()}
        if not isinstance(model_id, str) or model_id not in choices or "hub" not in choices[model_id]:
            raise ValueError("Select a downloadable model from the catalogue; browse a local manifest for custom ONNX exports")
        with self.lock:
            if self.state["state"] in ("downloading", "registering"):
                raise ValueError("A model download is already running")
            self.state = {"state": "downloading", "id": model_id, "bytes": 0}
            threading.Thread(target=self._download, args=(choices[model_id],), daemon=True).start()

    def _file(self, url, destination, model_id):
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + "." + uuid.uuid4().hex + ".download")
        try:
            with urlopen(url, timeout=60) as response, temporary.open("xb") as output:
                size = int(response.headers.get("Content-Length", "0")); received = 0
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    output.write(chunk); received += len(chunk)
                    self._set(state="downloading", id=model_id, bytes=received, total_bytes=size)
                if size and received != size:
                    raise ValueError("Incomplete model download")
                output.flush(); os.fsync(output.fileno())
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)

    def _download(self, row):
        try:
            root = self.root.parent / "third_party"
            repo = root / ("Depth-Anything-3" if row["family"] == "da3" else "Video-Depth-Anything")
            if not repo.is_dir():
                raise ValueError(f"Install the official source checkout at {repo} first; see model setup")
            remote_file = "model.safetensors" if row["family"] == "da3" else row["file"]
            destination = self.root / row["file"]
            self._file(f"https://huggingface.co/{row['hub']}/resolve/main/{remote_file}", destination, row["id"])
            self._set(state="registering", id=row["id"])
            self.register(row["id"])
            self._set(state="complete", id=row["id"])
        except Exception as exc:
            self._set(state="failed", id=row["id"], error=f"{type(exc).__name__}: {exc}")

    def register(self, model_id):
        row = next((r for r in catalog() if r["id"] == model_id), None)
        if not row or row["family"] == "onnx":
            raise ValueError("Use register_onnx_model.py for custom ONNX models")
        filename = self.root / row["file"]
        repo = self.root.parent / "third_party" / ("Depth-Anything-3" if row["family"] == "da3" else "Video-Depth-Anything")
        manifest = {"id": row["id"], "file": row["file"], "sha256": digest_file(filename),
                    "repository": os.path.relpath(repo, self.root).replace(chr(92), "/"), "license": row["license"], "hub": row["hub"]}
        if row["family"] == "da3":
            package = repo / "src" / "depth_anything_3"
            files = sorted(list(package.rglob("*.py")) + list(package.rglob("*.yaml")))
            manifest.update(contract="da3-distance-v1", architecture=row["architecture"],
                            source_sha256=identity({str(p.relative_to(repo)).replace('\\', '/'): digest_file(p) for p in files}))
        else:
            files = sorted(list((repo / "video_depth_anything").rglob("*.py")) + list((repo / "utils").rglob("*.py")))
            manifest.update(contract="video-depth-anything-stream-v1", encoder=row["encoder"], metric=row["metric"],
                            source_sha256=identity({p.relative_to(repo).as_posix(): digest_file(p) for p in files}))
        if not files:
            raise ValueError("Model source checkout missing")
        atomic_json(self.root / row["manifest"], manifest)
        return self.root / row["manifest"]
