"""Synchronous, content-addressed inference for Premiere frame render requests.

Only model-sized RGB guides cross this boundary. Original float color stays native.
No stereo controls participate in the cache key. A missing frame is inferred before
the render returns, so exports cannot silently use a placeholder depth map.
"""
import hashlib
import json
from pathlib import Path
import struct
import threading
from fractions import Fraction
from collections import OrderedDict
from .media import Media, image_depth, sequence_files, IMAGES

import numpy as np

from .cache import atomic_json, identity, read_depth, write_depth
from .catalog import catalog
from .inference import create_provider
from .devices import requested_device, resolve_device

REQUEST = struct.Struct("<4sIIIII")
RESPONSE = struct.Struct("<4sIIffI")
QUALITIES = ("Preview", "Balanced", "High", "Maximum")


class RenderService:
    def __init__(self, root, models_root, provider_factory=create_provider, device=None):
        self.root, self.models = Path(root) / "render", Path(models_root)
        self.factory = provider_factory
        self.device = requested_device(device)
        self.lock = threading.RLock()
        self.provider = None
        self.signature = None
        self.inferred = self.reused = 0
        self.external_media = OrderedDict()
        self.external_decoders = OrderedDict()
        self.planes = OrderedDict()
        self.plane_bytes = 0
        self.ranges = OrderedDict()

    def decoder(self, key, media, frame):
        value = self.external_decoders.get(key)
        if value is not None and value[0] > frame:
            value[1].close()
            del self.external_decoders[key]
            value = None
        if value is None:
            while len(self.external_decoders) >= 2:
                _, old = self.external_decoders.popitem(last=False)
                old[1].close()  # Close the suspended PyAV container explicitly.
            value = [0, iter(media.frames())]
            self.external_decoders[key] = value
        self.external_decoders.move_to_end(key)
        return value

    def plane(self, path, loader):
        stamp = path.stat()
        key = (str(path), stamp.st_mtime_ns, stamp.st_size)
        if key in self.planes:
            self.planes.move_to_end(key)
            return self.planes[key]
        value = loader(path)
        while self.planes and self.plane_bytes + value.nbytes > 128 * 1024 * 1024:
            _, removed = self.planes.popitem(last=False)
            self.plane_bytes -= removed.nbytes
        if value.nbytes <= 128 * 1024 * 1024:
            self.planes[key] = value;self.plane_bytes += value.nbytes
        return value

    def fixed_range(self, path, loader):
        stamp = path.stat();key=(str(path),stamp.st_mtime_ns,stamp.st_size)
        if key not in self.ranges:
            low, high = map(float,np.percentile(self.plane(path,loader),[1,99]))
            self.ranges[key]=(low,max(high,low+1e-6))
        self.ranges.move_to_end(key)
        while len(self.ranges) > 64:
            self.ranges.popitem(last=False)
        return self.ranges[key]

    def register_external(self, filename, import_mode="auto"):
        path = Path(filename).resolve(strict=True)
        if import_mode not in ("auto", "sequence", "still"):
            raise ValueError("Invalid external depth import mode")
        if not path.is_file():
            raise ValueError("Select a depth image or video file")
        record = {"path": str(path), "import_mode": import_mode}
        if path.suffix.lower() in IMAGES:
            files = [path] if import_mode == "still" else sequence_files(path)
            image_depth(files[0])  # Report invalid depth at import, before rendering.
            record["frame_count"] = len(files)
        elif import_mode != "auto":
            raise ValueError("Image sequence import requires PNG, TIFF, EXR or PFM images")
        with self.lock:
            folder = self.root / "external"
            folder.mkdir(parents=True, exist_ok=True)
            for file in folder.glob("*.json"):
                existing = json.loads(file.read_text())
                if existing["path"] == str(path) and existing.get("import_mode", "auto") == import_mode:
                    return int(file.stem)
            index = max([int(p.stem) for p in folder.glob("*.json")] + [0]) + 1
            if index > 999999:
                raise ValueError("External depth source slots exhausted")
            atomic_json(folder / f"{index}.json", record)
            return index

    def external(self, request):
        index = request["id"]
        if not isinstance(index, int) or not 1 <= index <= 999999:
            raise ValueError("Choose an external source with Browse Depth Source first")
        scale, num, den = request["scale"], request["fps_num"], request["fps_den"]
        if min(scale, num, den) <= 0:
            raise ValueError("Invalid source timing")
        fps = Fraction(num, den)
        frame = int(Fraction(request["time"], scale) * fps // 1) + int(request["offset"])
        if frame < 0:
            raise ValueError("Depth frame precedes the selected sequence")
        with self.lock:
            record = json.loads((self.root / "external" / f"{index}.json").read_text())
            filename = record["path"]
            signature = (index, str(fps), Path(filename).stat().st_mtime_ns)
            if signature not in self.external_media:
                self.external_media[signature] = Media(filename, fps, depth=True, image_sequence=record.get("import_mode") != "still")
            self.external_media.move_to_end(signature)
            while len(self.external_media) > 8:
                self.external_media.popitem(last=False)
            media = self.external_media[signature]
            if media.files:
                # A single unnumbered image is a static depth map; sequences remain frame accurate.
                if len(media.files) == 1 and record.get("import_mode") != "sequence":
                    frame = 0
                if frame >= len(media.files):
                    raise ValueError("External depth sequence is shorter than this clip/time; adjust Depth Frame Offset")
                depth = self.plane(media.files[frame],image_depth)
                low,high = self.fixed_range(media.files[0],image_depth)
                hit = True
            else:
                stamp = media.path.stat()
                folder = self.root / "external-video" / identity({"path": filename, "mtime": stamp.st_mtime_ns, "size": stamp.st_size})
                target = folder / f"{frame:09d}.s3d"
                try:
                    depth = self.plane(target,read_depth);hit = True
                except (OSError, ValueError):
                    # Decode a verified CFR prefix once; subsequent renders reuse float depth files.
                    hit = False
                    found = False
                    decoder = self.decoder(str(folder), media, frame)
                    for decoded in decoder[1]:
                        write_depth(folder / f"{decoded.index:09d}.s3d", decoded.pixels)
                        decoder[0] = decoded.index + 1
                        if decoded.index == frame:
                            depth = decoded.pixels;found = True;break
                    if not found:
                        raise ValueError("External depth video is shorter than this clip/time")
                low,high = self.fixed_range(folder / "000000000.s3d",read_depth)
            h, w = depth.shape
            return RESPONSE.pack(b"SDD1", w, h, low, high, int(hit)) + np.asarray(depth, dtype="<f4").tobytes()

    def frame(self, body):
        if len(body) < REQUEST.size:
            raise ValueError("Incomplete render request")
        magic, w, h, model, quality, source_length = REQUEST.unpack_from(body)
        rows = catalog()
        if magic != b"SDR1" or not 1 <= w <= 2048 or not 1 <= h <= 2048 or model >= len(rows) or quality >= 4 or source_length > 32768:
            raise ValueError("Invalid render request")
        if len(body) != REQUEST.size + source_length + w*h*3:
            raise ValueError("Invalid render payload length")
        source = body[REQUEST.size:REQUEST.size+source_length].decode("utf-8")
        rgb_bytes = body[REQUEST.size+source_length:]
        manifest_path = self.models / rows[model]["manifest"]
        if not manifest_path.is_file():
            raise ValueError(f"{rows[model]['name']} is not installed. Download it in the 2D to 3D panel.")
        manifest_bytes = manifest_path.read_bytes()
        manifest = json.loads(manifest_bytes)
        if manifest.get("contract") == "video-depth-anything-stream-v1":
            raise ValueError("Video Depth Anything needs ordered video context. Generate its cache in the panel, or choose DA-V2 / DA3 for automatic rendering.")
        weights = manifest_path.parent / manifest["file"]
        stamp = weights.stat()
        device = resolve_device(self.device, backend="torch" if manifest.get("contract") == "da3-distance-v1" else "onnx")
        generation = identity({"manifest": hashlib.sha256(manifest_bytes).hexdigest(), "size": stamp.st_size,
                               "modified": stamp.st_mtime_ns, "quality": QUALITIES[quality],
                               "preprocess": "premiere-sdr-guide-v2-heuristic-cudnn", "device": device})
        key = hashlib.sha256(generation.encode() + struct.pack("<II", w, h) + rgb_bytes).hexdigest()
        entry = self.root / generation
        frame_path = entry / "frames" / (key + ".s3d")
        # Freeze one range per source/settings to avoid per-frame normalization pumping.
        source_stamp = source
        try:
            original = Path(source).stat()
            source_stamp += f":{original.st_size}:{original.st_mtime_ns}"
        except OSError:
            pass
        anchor_path = entry / "ranges" / (hashlib.sha256(source_stamp.encode()).hexdigest() + ".json")
        with self.lock:
            hit = False
            try:
                depth = self.plane(frame_path,read_depth)
                hit = True
            except (OSError, ValueError):
                if self.signature != generation:
                    self.provider = None
                    self.provider = self.factory(manifest_path, QUALITIES[quality], device)
                    self.signature = generation
                rgb = np.frombuffer(rgb_bytes, dtype=np.uint8).reshape(h, w, 3)
                depth = self.provider.infer(rgb)
                write_depth(frame_path, depth)
                self.inferred += 1
            if hit:
                self.reused += 1
            try:
                anchor = json.loads(anchor_path.read_text())
                low, high = float(anchor["low"]), float(anchor["high"])
                if not np.isfinite([low, high]).all() or high <= low:
                    raise ValueError("Invalid range")
            except (OSError, ValueError, KeyError):
                low, high = map(float, np.percentile(depth, [1, 99]))
                high = max(high, low + 1e-6)
                atomic_json(anchor_path, {"low": low, "high": high, "anchor": key})
            dh, dw = depth.shape
            return RESPONSE.pack(b"SDD1", dw, dh, low, high, int(hit)) + np.asarray(depth, dtype="<f4").tobytes()
