from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
import re

import av
import numpy as np
from PIL import Image

from .cache import digest_file, identity

IMAGES = {".png", ".tif", ".tiff", ".exr", ".pfm"}


@dataclass
class Frame:
    index: int
    pts: Fraction
    pixels: np.ndarray


def sequence_files(first: Path) -> list[Path]:
    """An explicitly selected first numbered image defines sequence start."""
    first = first.resolve(strict=True)
    match = re.fullmatch(r"(.*?)(\d+)", first.stem)
    if not match:
        return [first]
    prefix, digits = match.groups()
    pattern = re.compile(re.escape(prefix) + r"(\d{" + str(len(digits)) + r"})" + re.escape(first.suffix) + r"\Z", re.I)
    entries = {}
    for file in first.parent.iterdir():
        found = pattern.fullmatch(file.name)
        if found and int(found[1]) >= int(digits):
            entries[int(found[1])] = file
    indices = sorted(entries)
    if indices != list(range(int(digits), int(digits) + len(indices))):
        raise ValueError("Image sequence has missing frame numbers")
    return [entries[i] for i in indices]


def image_depth(path: Path, channel: str = "Z") -> np.ndarray:
    if path.suffix.lower() == ".exr":
        try:
            import OpenImageIO as oiio
        except ImportError as exc:
            raise RuntimeError("Install the exr extra for floating-point EXR depth") from exc
        image = oiio.ImageInput.open(str(path))
        if image is None:
            raise ValueError("Cannot open EXR depth")
        try:
            spec = image.spec()
            channels = list(spec.channelnames)
            # Never silently substitute a beauty channel for a requested Z pass.
            if channel not in channels:
                if len(channels) == 1:
                    index = 0
                else:
                    raise ValueError(f"EXR channel {channel!r} absent; available: {channels}")
            else:
                index = channels.index(channel)
            a = np.asarray(image.read_image(format=oiio.FLOAT), dtype=np.float32)[..., index]
        finally:
            image.close()
    elif path.suffix.lower() == ".pfm":
        import cv2
        a = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
        if a is None:
            raise ValueError("Cannot open PFM depth")
        if a.ndim == 3:
            a = a[..., ::-1]  # OpenCV returns BGR; use RGB luminance below.
    else:
        with Image.open(path) as image:
            if image.mode in ("P", "PA", "CMYK", "YCbCr"):
                image = image.convert("RGB")
            a = np.asarray(image)
        # Pillow may expose 16-bit PNG as int32, so use file precision where needed.
        if a.dtype.kind in "ui":
            ceiling = 255 if a.dtype.itemsize == 1 else 65535
            if a.min() < 0 or a.max() > ceiling:
                raise ValueError("Integer depth outside supported 8/16-bit range")
            a = a.astype(np.float32) / ceiling
    if a.ndim == 3:
        if a.shape[-1] in (1, 2):
            a = a[..., 0]  # Grayscale or grayscale + alpha.
        elif a.shape[-1] in (3, 4):
            rgb = np.asarray(a[..., :3], dtype=np.float32)
            if np.array_equal(rgb[..., 0], rgb[..., 1]) and np.array_equal(rgb[..., 0], rgb[..., 2]):
                a = rgb[..., 0]  # Preserve replicated grayscale exactly.
            else:
                # Sample RGB as intensity on demand. No converted files, no
                # sequence preprocessing, and no gamma/transfer conversion.
                a = rgb[..., 0]*.2126 + rgb[..., 1]*.7152 + rgb[..., 2]*.0722
        else:
            raise ValueError("Unsupported depth image channel count")
    if a.ndim != 2:
        raise ValueError("Expected a single-channel depth image")
    # EXR infinity often denotes sky: preserve as far endpoint in a separate policy.
    # Reject here so an arbitrary near/far interpretation is never silently applied.
    if not np.isfinite(a).all():
        raise ValueError("Depth contains NaN/infinity; render a finite far clip/Mist pass first")
    return np.asarray(a, dtype=np.float32)


class Media:
    def __init__(self, path: str, fps: Fraction | None = None, depth=False, channel="Z", require_sdr=True, image_sequence=True):
        self.path = Path(path).expanduser().resolve(strict=True)
        self.depth, self.channel = depth, channel
        self.files = (sequence_files(self.path) if image_sequence else [self.path]) if self.path.suffix.lower() in IMAGES else None
        if self.files:
            if fps is None or fps <= 0:
                raise ValueError("Image sequences require an explicit rational frame rate")
            self.fps = fps
        else:
            with av.open(str(self.path)) as container:
                if not container.streams.video:
                    raise ValueError("No video stream")
                stream = container.streams.video[0]
                self.fps = Fraction(stream.average_rate or 0)
                if self.fps <= 0:
                    raise ValueError("Video has no usable frame rate")
                if fps is not None and self.fps != fps:
                    raise ValueError(f"Frame rate mismatch: {self.fps} versus {fps}")
                # Depth inference currently accepts only an SDR model proxy; color render stays in Premiere.
                if require_sdr and not depth and stream.codec_context.color_trc in (16, 18):
                    raise ValueError("HDR inference requires an SDR analysis proxy; use external depth or an SDR proxy file")

    def fingerprint(self) -> str:
        files = self.files or [self.path]
        return identity({"files": [digest_file(p) for p in files], "fps": str(self.fps),
                         "depth": self.depth, "channel": self.channel, "decode": "v1"})

    def frames(self):
        if self.files:
            for i, path in enumerate(self.files):
                if self.depth:
                    pixels = image_depth(path, self.channel)
                else:
                    with Image.open(path) as image:
                        pixels = np.asarray(image.convert("RGB"), dtype=np.uint8)
                yield Frame(i, Fraction(i, 1) / self.fps, pixels)
            return
        with av.open(str(self.path)) as container:
            stream = container.streams.video[0]
            origin = None
            for i, frame in enumerate(container.decode(stream)):
                if frame.pts is None:
                    raise ValueError("Missing presentation timestamp")
                pts = Fraction(frame.pts) * frame.time_base
                if origin is None:
                    origin = pts
                pts -= origin
                # Reject VFR instead of silently drifting from Premiere's source timing.
                if abs(pts - Fraction(i, 1) / self.fps) > Fraction(1, 1000):
                    raise ValueError("Variable-frame-rate media: transcode to CFR before caching")
                if self.depth:
                    pixels = frame.to_ndarray(format="gray16le").astype(np.float32) / 65535
                else:
                    pixels = frame.to_ndarray(format="rgb24")
                yield Frame(i, pts, pixels)
