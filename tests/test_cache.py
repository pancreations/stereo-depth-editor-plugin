from fractions import Fraction
from pathlib import Path
import struct

import numpy as np
import pytest

from stereodepth_worker.cache import Cache, identity, write_depth, read_depth
from stereodepth_worker.media import image_depth, sequence_files, Media
from PIL import Image


def test_float_roundtrip_and_corruption(tmp_path):
    path = tmp_path / "frame.s3d"
    a = np.array([[-4, .125, 1], [2500, .5, 0]], dtype=np.float32)
    write_depth(path, a)
    np.testing.assert_array_equal(a, read_depth(path))
    data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
    with pytest.raises(ValueError, match="checksum"):
        read_depth(path)


@pytest.mark.parametrize("header", [b"", b"invalid", struct.pack("<4sIIIII", b"S3D1", 1, 0xffffffff, 8, 1, 0)])
def test_bad_headers(tmp_path, header):
    path = tmp_path / "bad.s3d"; path.write_bytes(header)
    with pytest.raises(ValueError):
        read_depth(path)


def test_identity_and_safe_clear(tmp_path):
    cache = Cache(tmp_path / "cache")
    key = identity({"source": "A", "model": "B"})
    assert key == identity({"model": "B", "source": "A"})
    assert key != identity({"source": "A", "model": "C"})
    with pytest.raises(ValueError):
        cache.clear("../outside")
    write_depth(cache.frame(key, 0), np.ones((3, 4), dtype=np.float32))
    cache.complete(key, dict(frames=1, width=4, height=3, fps_num=24000, fps_den=1001, external=True, low=0, high=1))
    slot = cache.bind(key)
    assert cache.bind(key) == slot
    cache.clear(key)
    assert not cache.entry(key).exists()
    # Binding is kept so a project never renders another source under the same ID.
    assert (cache.root / "bindings" / f"{slot}.txt").exists()


def test_cache_cannot_clear_unexpected_files(tmp_path):
    cache = Cache(tmp_path); key = "a" * 64
    directory = cache.entry(key); directory.mkdir()
    (directory / "precious.txt").write_text("keep")
    with pytest.raises(ValueError, match="Unexpected"):
        cache.clear(key)
    assert (directory / "precious.txt").read_text() == "keep"


def test_png_16bit_precision_and_sequence_gaps(tmp_path):
    a = np.array([[0, 32768, 65535]], dtype=np.uint16)
    first = tmp_path / "depth_0100.png"
    Image.fromarray(a).save(first)
    d = image_depth(first)
    assert d[0, 1] == np.float32(32768 / 65535)
    assert d[0, 2] == 1
    Image.fromarray(a).save(tmp_path / "depth_0102.png")
    with pytest.raises(ValueError, match="missing"):
        sequence_files(first)


def test_color_depth_uses_documented_intensity(tmp_path):
    path = tmp_path / "depth.png"
    Image.new("RGB", (4, 4), (255, 0, 0)).save(path)
    np.testing.assert_allclose(image_depth(path), .2126, atol=1e-6)


def test_rational_sequence_time(tmp_path):
    for i in range(3):
        Image.new("L", (4, 3), i).save(tmp_path / f"d{i:04d}.png")
    frames = list(Media(str(tmp_path / "d0000.png"), Fraction(30000, 1001), depth=True).frames())
    assert frames[2].pts == Fraction(2002, 30000)
