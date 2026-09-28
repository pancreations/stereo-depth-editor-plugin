from __future__ import annotations

import argparse
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import threading
import socket

from .cache import Cache, atomic_json
from .jobs import Jobs
from .catalog import Models
from .render_service import RenderService
from .preview import open_preview
from .devices import DEVICES, requested_device, default_cache_root


def make_server(root: Path, port=48731, models_root=None, device=None):
    cache = Cache(root)
    jobs = Jobs(cache, device=device)
    token = secrets.token_urlsafe(32)
    models = Models(models_root or (root / "models"))
    submission_lock = threading.Lock()
    renderer = RenderService(cache.root, models.root, device=device)

    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def log_message(self, *_):
            pass  # Never log tokens or private source paths.

        def reply(self, status, value):
            data = json.dumps(value, allow_nan=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def authorized(self):
            if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + token):
                self.reply(401, {"error": "Connect using the worker connection file"})
                return False
            return True

        def do_GET(self):
            if not self.authorized():
                return
            try:
                if self.path == "/v1/status":
                    self.reply(200, {"version": "0.3.1", "cache_root": str(cache.root), "active": jobs.active,
                                     "render_cache": {"inferred": renderer.inferred, "reused": renderer.reused},
                                     "capabilities": {"onnx": True, "external_depth": True, "ai_inpainting": False,
                                                      "host_gpu": False, "device": renderer.device,
                                                      "sbs_preview": os.name == "nt"}})
                elif self.path.startswith("/v1/jobs/"):
                    self.reply(200, jobs.status(self.path.removeprefix("/v1/jobs/")))
                elif self.path == "/v1/models":
                    self.reply(200, models.listing())
                else:
                    self.reply(404, {"error": "Unknown endpoint"})
            except (ValueError, OSError) as exc:
                self.reply(400, {"error": str(exc)})

        def do_POST(self):
            if not self.authorized():
                return
            try:
                if self.path == "/v1/render/depth":
                    if self.headers.get("Content-Type") != "application/octet-stream":
                        raise ValueError("Expected binary render request")
                    length = int(self.headers.get("Content-Length", "0"))
                    if not 24 <= length <= 13_000_000:
                        raise ValueError("Invalid render request size")
                    self.connection.settimeout(120)
                    with submission_lock:
                        if jobs.active or models.status()["state"] in ("downloading", "registering"):
                            raise ValueError("Depth cache preparation is running; wait for it to finish")
                        payload = renderer.frame(self.rfile.read(length))
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(payload)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(payload)
                    return
                if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                    raise ValueError("Expected application/json")
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 65536:
                    raise ValueError("Invalid request size")
                self.connection.settimeout(10)
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError("Expected a JSON object")
                if self.path == "/v1/preview/open":
                    self.reply(200, open_preview())
                elif self.path == "/v1/render/register-external":
                    self.reply(200, {"id": renderer.register_external(data["path"], data.get("import_mode", "auto"))})
                elif self.path == "/v1/render/external-name":
                    index = data["id"]
                    if not isinstance(index, int) or not 1 <= index <= 999999:
                        raise ValueError("Invalid external depth source")
                    source = json.loads((renderer.root / "external" / f"{index}.json").read_text())
                    label = Path(source["path"]).name
                    if source.get("import_mode") == "sequence":
                        label = f"Sequence ({source.get('frame_count', '?')} frames): {label}"
                    elif source.get("import_mode") == "still":
                        label = f"Still: {label}"
                    payload = label.encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers();self.wfile.write(payload)
                elif self.path == "/v1/render/external":
                    payload = renderer.external(data)
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                elif self.path == "/v1/jobs":
                    with submission_lock:
                        if models.status()["state"] in ("downloading", "registering"):
                            raise ValueError("Wait for the selected model download to finish")
                        job_id = jobs.submit(data)
                    self.reply(202, {"id": job_id})
                elif self.path == "/v1/models/download":
                    with submission_lock:
                        if jobs.active is not None:
                            raise ValueError("Wait for the active depth job before downloading models")
                        models.submit(data["id"])
                    self.reply(202, {"accepted": True})
                elif self.path == "/v1/cancel":
                    jobs.cancel(data["id"])
                    self.reply(200, {"cancelled": True})
                elif self.path == "/v1/cache/clear":
                    jobs.clear(data["key"])
                    self.reply(200, {"cleared": True})
                else:
                    self.reply(404, {"error": "Unknown endpoint"})
            except (ValueError, KeyError, OSError, RuntimeError) as exc:
                self.reply(400, {"error": str(exc)})
            except Exception as exc:
                self.reply(500, {"error": f"{type(exc).__name__}: {exc}"})

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server, jobs, {"url": f"http://127.0.0.1:{server.server_port}", "token": token}


def main():
    parser = argparse.ArgumentParser(description="StereoDepth local depth worker")
    default_root = default_cache_root()
    parser.add_argument("--device", choices=DEVICES, default=requested_device())
    parser.add_argument("--cache", type=Path, default=default_root)
    parser.add_argument("--port", type=int, default=48731)
    parser.add_argument("--connection", type=Path, default=Path("runtime/connection.json"))
    parser.add_argument("--models", type=Path, default=Path("models"))
    args = parser.parse_args()
    # Exclusive process lock prevents two workers from writing the same cache.
    args.cache.mkdir(parents=True, exist_ok=True)
    lockfile = (args.cache / "worker.lock").open("a+b")
    try:
        if os.name == "nt":
            import msvcrt
            lockfile.seek(0)
            if not lockfile.read(1):
                lockfile.write(b"0")
                lockfile.flush()
            lockfile.seek(0)
            msvcrt.locking(lockfile.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        server, jobs, connection = make_server(args.cache, args.port, args.models, args.device)
        atomic_json(args.connection, connection)
        print(f"StereoDepth worker on {connection['url']}; connect with {args.connection.resolve()}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            jobs.cancel_event.set()
        finally:
            server.server_close()
            args.connection.unlink(missing_ok=True)
    finally:
        lockfile.close()


if __name__ == "__main__":
    main()
