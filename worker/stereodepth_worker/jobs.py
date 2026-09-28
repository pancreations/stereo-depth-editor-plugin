from __future__ import annotations

from dataclasses import asdict
from fractions import Fraction
from pathlib import Path
import threading
import uuid

import numpy as np

from .cache import Cache, atomic_json, identity, read_depth, write_depth
from .media import Media
from .temporal import Temporal, stabilize
from .devices import requested_device


class Cancelled(Exception):
    pass


class Jobs:
    def __init__(self, cache: Cache, provider_factory=None, device=None):
        self.cache = cache
        self.device = requested_device(device)
        self.provider_factory = provider_factory
        self.active = None
        self.lock = threading.RLock()
        self.cancel_event = threading.Event()
        self.states = {}

    def submit(self, request: dict) -> str:
        allowed = {"source", "depth_override", "depth_source", "fps", "model", "quality", "device",
                   "channel", "temporal", "depth_frame_offset"}
        if not isinstance(request, dict) or set(request) - allowed:
            raise ValueError("Unknown job fields")
        if not isinstance(request.get("source"), str):
            raise ValueError("A source path is required")
        if not isinstance(request.get("depth_override", False), bool):
            raise ValueError("Depth Pass Override must be a boolean")
        with self.lock:
            if self.active is not None:
                raise ValueError("A depth job is already running")
            job = uuid.uuid4().hex
            self.active = job
            self.cancel_event.clear()
            self._status(job, state="queued", completed=0)
            threading.Thread(target=self._run, args=(job, dict(request)), daemon=True).start()
            return job

    def status(self, job: str):
        if len(job) != 32 or any(c not in "0123456789abcdef" for c in job):
            raise ValueError("Invalid job ID")
        with self.lock:
            if job in self.states:
                return dict(self.states[job])
            import json
            return json.loads((self.cache.root / "jobs" / f"{job}.json").read_text())

    def cancel(self, job: str):
        with self.lock:
            if self.active != job:
                raise ValueError("Job is not running")
            self.cancel_event.set()

    def clear(self, key: str):
        with self.lock:
            if self.active is not None:
                raise ValueError("Wait for the active job before clearing caches")
            self.cache.clear(key)

    def _status(self, job, **fields):
        state = {"id": job, **fields}
        with self.lock:
            atomic_json(self.cache.root / "jobs" / f"{job}.json", state)
            self.states[job] = state

    def _check(self):
        if self.cancel_event.is_set():
            raise Cancelled("Depth generation cancelled; completed raw frames are reusable")

    def _run(self, job, request):
        try:
            self._generate(job, request)
        except Cancelled as exc:
            self._status(job, state="cancelled", error=str(exc))
        except Exception as exc:
            self._status(job, state="failed", error=f"{type(exc).__name__}: {exc}")
        finally:
            with self.lock:
                self.active = None

    def _generate(self, job, request):
        fps = Fraction(request["fps"]) if request.get("fps") else None
        external = request.get("depth_override", False)
        source = Media(request["source"], fps, require_sdr=not external)
        temporal = Temporal(**request.get("temporal", {}))
        offset = request.get("depth_frame_offset", 0)
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("Depth frame offset must be a nonnegative integer")
        self._status(job, state="fingerprinting", completed=0)
        source_key = source.fingerprint()
        self._check()
        depth_media = None
        provider = None
        if external:
            if not request.get("depth_source"):
                raise ValueError("Depth Pass Override needs an external depth source")
            depth_media = Media(request["depth_source"], source.fps, depth=True, channel=request.get("channel", "Z"))
            generation = {"external": depth_media.fingerprint(), "offset": offset}
        else:
            if not request.get("model"):
                raise ValueError("Select a model manifest or enable Depth Pass Override")
            if self.provider_factory:
                provider = self.provider_factory(request)
            else:
                from .inference import create_provider
                provider = create_provider(request["model"], request.get("quality", "Balanced"), request.get("device", self.device))
            generation = {"provider": provider.key}
        raw_key = identity({"version": 1, "source": source_key, "generation": generation, "fps": str(source.fps)})
        replay_stateful = False
        if getattr(provider, "stateful", False):
            # A skipped prefix cannot seed temporal model state. Validate the entire
            # published entry up front; partial/corrupt entries replay from frame zero.
            replay_stateful = True
            try:
                previous_metadata = self.cache.metadata(raw_key)
                if not previous_metadata.get("complete"):
                    raise ValueError("Incomplete video cache")
                for i in range(previous_metadata["frames"]):
                    self._check()
                    read_depth(self.cache.frame(raw_key, i))
                replay_stateful = False
            except (OSError, ValueError, KeyError):
                provider.reset()
        depth_frames = iter(depth_media.frames()) if external else None
        if depth_frames:
            for _ in range(offset):
                if next(depth_frames, None) is None:
                    raise ValueError("External depth offset is past end of sequence")
        count = 0
        samples = []
        shape = None
        for frame in source.frames():
            self._check()
            count += 1
            depth_frame = next(depth_frames, None) if depth_frames else None
            if external and depth_frame is None:
                raise ValueError("External depth is shorter than the source; no implicit hold-last-frame")
            path = self.cache.frame(raw_key, frame.index)
            try:
                if replay_stateful:
                    raise FileNotFoundError("Replay ordered model context")
                depth = read_depth(path)
            except (OSError, ValueError):
                if getattr(provider, "stateful", False) and not replay_stateful:
                    raise RuntimeError("Video cache changed while reading; restart generation to replay temporal context")
                depth = depth_frame.pixels if external else provider.infer(frame.pixels)
                write_depth(path, depth)
            if depth.shape != frame.pixels.shape[:2]:
                raise ValueError("Depth/source sizes differ; render depth at source resolution")
            if shape is not None and depth.shape != shape:
                raise ValueError("Source dimensions change during sequence")
            shape = depth.shape
            # Bounded deterministic sampling across the clip; fixed normalization avoids pumping.
            if len(samples) < 4096:
                samples.append(depth[::max(1, shape[0] // 16), ::max(1, shape[1] // 16)].ravel()[:256])
            self._status(job, state="generating", completed=count, raw_key=raw_key)
        if not count:
            raise ValueError("Source contains no frames")
        values = np.concatenate(samples)
        low, high = map(float, np.percentile(values, [.5, 99.5]))
        if high - low < 1e-6:
            high = low + 1
        metadata = {"frames": count, "width": shape[1], "height": shape[0], "fps_num": source.fps.numerator,
                    "fps_den": source.fps.denominator, "external": external, "low": low, "high": high,
                    "source": source_key, "generation": generation, "temporal": None}
        self.cache.complete(raw_key, metadata)
        final_key = raw_key
        if temporal.enabled:
            final_key = identity({"raw": raw_key, "temporal": asdict(temporal), "algorithm": "flow-pair-v1"})
            previous, previous_rgb = None, None
            for frame in source.frames():
                self._check()
                raw = read_depth(self.cache.frame(raw_key, frame.index))
                normalized = ((raw - low) / (high - low)).astype(np.float32)
                filtered = stabilize(normalized, previous, frame.pixels, previous_rgb, temporal)
                write_depth(self.cache.frame(final_key, frame.index), filtered * (high - low) + low)
                previous, previous_rgb = normalized, frame.pixels
                self._status(job, state="stabilizing", completed=frame.index + 1, total=count, raw_key=raw_key)
            self.cache.complete(final_key, {**metadata, "temporal": asdict(temporal), "raw_key": raw_key})
        self._check()
        slot = self.cache.bind(final_key)
        self._status(job, state="complete", completed=count, total=count, key=final_key, raw_key=raw_key,
                     slot=slot, external=external, fps=str(source.fps), width=shape[1], height=shape[0])
