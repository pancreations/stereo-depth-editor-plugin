from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import threading
import time
import uuid
import zlib

import numpy as np

HEADER = struct.Struct("<4sIIIII")
KEY = re.compile(r"[0-9a-f]{64}\Z")


def digest_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def identity(settings: dict) -> str:
    return hashlib.sha256(json.dumps(settings, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def atomic_bytes(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temp.open("xb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        # Windows readers/AV may briefly hold a destination without delete sharing.
        # Keep the old complete file visible and retry the atomic rename.
        for attempt in range(20):
            try:
                os.replace(temp, path)
                break
            except PermissionError:
                if attempt == 19:
                    raise
                time.sleep(.01 * (attempt + 1))
    finally:
        temp.unlink(missing_ok=True)


def atomic_json(path: Path, data: dict):
    atomic_bytes(path, json.dumps(data, indent=2, allow_nan=False).encode())


def write_depth(path: Path, depth: np.ndarray):
    a = np.asarray(depth, dtype="<f4", order="C")
    if a.ndim != 2 or not np.isfinite(a).all():
        raise ValueError("Depth must be a finite HxW float plane")
    h, w = a.shape
    if min(w, h) <= 0 or max(w, h) > 32768 or w * h > 268435456:
        raise ValueError("Invalid depth dimensions")
    payload = a.tobytes()
    atomic_bytes(path, HEADER.pack(b"S3D1", 1, w, h, 1, zlib.crc32(payload)) + payload)


def read_depth(path: Path) -> np.ndarray:
    with path.open("rb") as f:
        header = f.read(HEADER.size)
        if len(header) != HEADER.size:
            raise ValueError("Truncated depth header")
        magic, version, w, h, kind, crc = HEADER.unpack(header)
        if (magic, version, kind) != (b"S3D1", 1, 1) or min(w, h) < 1 or max(w, h) > 32768 or w * h > 268435456:
            raise ValueError("Invalid depth header")
        if path.stat().st_size != HEADER.size + w * h * 4:
            raise ValueError("Invalid depth payload size")
        payload = f.read()
    if zlib.crc32(payload) != crc:
        raise ValueError("Corrupt depth cache checksum")
    a = np.frombuffer(payload, dtype="<f4").reshape(h, w).copy()
    if not np.isfinite(a).all():
        raise ValueError("Non-finite cached depth")
    return a


class Cache:
    """Single worker writer, atomic frames, immutable content identities and bindings."""

    def __init__(self, root: Path):
        self.root = root.resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        for name in ("entries", "bindings", "jobs"):
            (self.root / name).mkdir(exist_ok=True)

    def entry(self, key: str) -> Path:
        if not KEY.fullmatch(key):
            raise ValueError("Invalid cache identity")
        path = (self.root / "entries" / key).resolve()
        if path.parent != (self.root / "entries").resolve():
            raise ValueError("Cache path escapes root")
        return path

    def frame(self, key: str, index: int) -> Path:
        if not isinstance(index, int) or not 0 <= index < 1_000_000_000:
            raise ValueError("Invalid frame index")
        return self.entry(key) / f"{index:09d}.s3d"

    def metadata(self, key: str) -> dict:
        return json.loads((self.entry(key) / "manifest.json").read_text())

    def complete(self, key: str, metadata: dict):
        # Verify every frame before publishing a complete cache.
        for i in range(metadata["frames"]):
            depth = read_depth(self.frame(key, i))
            if depth.shape != (metadata["height"], metadata["width"]):
                raise ValueError("Inconsistent cache frame dimensions")
        atomic_json(self.entry(key) / "manifest.json", {**metadata, "key": key, "complete": True})

    def bind(self, key: str) -> int:
        with self.lock:
            m = self.metadata(key)
            if not m.get("complete"):
                raise ValueError("Cache is incomplete")
            bindings = self.root / "bindings"
            existing = list(bindings.glob("*.txt"))
            for file in existing:
                if file.read_text().split()[1:2] == [key]:
                    return int(file.stem)
            slot = max([int(p.stem) for p in existing if p.stem.isdigit()] + [0]) + 1
            if slot > 999999:
                raise ValueError("Cache binding slots exhausted")
            values = ["S3DB1", key, m["fps_num"], m["fps_den"], m["frames"], m["width"], m["height"],
                      int(m["external"]), m["low"], m["high"]]
            atomic_bytes(bindings / f"{slot}.txt", ("\n".join(map(str, values)) + "\n").encode())
            return slot

    def clear(self, key: str):
        # Never delete an arbitrary directory, follow symlinks, or touch other entries.
        with self.lock:
            directory = self.entry(key)
            if not directory.exists():
                return
            files = list(directory.iterdir())
            for file in files:
                if file.is_symlink() or file.is_dir() or file.resolve().parent != directory:
                    raise ValueError("Unexpected cache entry; refusing cleanup")
                if file.name != "manifest.json" and not re.fullmatch(r"\d{9}\.s3d", file.name):
                    raise ValueError("Unexpected cache file; refusing cleanup")
            for file in files:
                file.unlink()
            directory.rmdir()
            # Bindings intentionally survive: IDs are never reused for a different source.
