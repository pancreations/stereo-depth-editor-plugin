from fractions import Fraction

import av
import numpy as np
import pytest

from stereodepth_worker.media import Media, image_depth


def test_lossless_depth_video_frame_order(tmp_path):
    path = tmp_path / "depth.mkv"
    with av.open(str(path), "w") as container:
        stream = container.add_stream("ffv1", rate=24)
        stream.width = 32; stream.height = 24; stream.pix_fmt = "gray16le"
        for i in range(4):
            a = np.full((24, 32), 1000 + i * 1000, dtype=np.uint16)
            frame = av.VideoFrame.from_ndarray(a, format="gray16le")
            frame.pts = i; frame.time_base = Fraction(1, 24)
            for packet in stream.encode(frame):
                container.mux(packet)
        for packet in stream.encode():
            container.mux(packet)
    frames = list(Media(str(path), Fraction(24), depth=True).frames())
    assert len(frames) == 4
    for i, frame in enumerate(frames):
        assert frame.index == i
        np.testing.assert_allclose(frame.pixels, (1000 + i * 1000) / 65535, atol=1e-7)
    with pytest.raises(ValueError, match="rate mismatch"):
        Media(str(path), Fraction(25), depth=True)


def test_exr_float_channel_preserves_metric_precision(tmp_path):
    oiio = pytest.importorskip("OpenImageIO")
    path = tmp_path / "depth.exr"
    values = np.array([[.125, 1.5, 4000], [.25, 17, 100000]], dtype=np.float32)
    spec = oiio.ImageSpec(3, 2, 2, oiio.FLOAT)
    spec.channelnames = ["R", "Z"]
    output = oiio.ImageOutput.create(str(path))
    assert output.open(str(path), spec)
    assert output.write_image(np.stack([np.zeros_like(values), values], axis=-1))
    output.close()
    np.testing.assert_array_equal(image_depth(path, "Z"), values)
    with pytest.raises(ValueError, match="absent"):
        image_depth(path, "MissingChannel")


def test_float_tiff_not_normalized_as_color(tmp_path):
    from PIL import Image
    path = tmp_path / "depth.tiff"
    values = np.array([[.125, 1.5, 4000]], dtype=np.float32)
    Image.fromarray(values).save(path)
    np.testing.assert_array_equal(image_depth(path), values)


@pytest.mark.parametrize("mode", ["RGB", "RGBA"])
def test_color_depth_is_read_as_intensity_without_writing_files(tmp_path, mode):
    from PIL import Image
    rgb = np.array([[[255, 0, 0], [0, 255, 0], [0, 0, 255], [128, 128, 128]]], dtype=np.uint8)
    pixels = np.concatenate([rgb, np.zeros((1, 4, 1), np.uint8)], axis=-1) if mode == "RGBA" else rgb
    path = tmp_path/"colored.png"
    Image.fromarray(pixels).save(path)
    original = path.read_bytes()
    np.testing.assert_allclose(image_depth(path), [[.2126, .7152, .0722, 128/255]], atol=1e-7)
    assert path.read_bytes() == original
    assert list(tmp_path.iterdir()) == [path]


def test_gray_alpha_and_palette_depth(tmp_path):
    from PIL import Image
    gray_alpha = np.array([[[50, 0], [100, 255]]], np.uint8)
    path = tmp_path/"alpha.png"
    Image.fromarray(gray_alpha).save(path)
    np.testing.assert_allclose(image_depth(path), [[50/255, 100/255]])
    palette = Image.new("P", (2, 1))
    palette.putpalette([255, 0, 0, 0, 255, 0]+[0]*762)
    palette.putdata([0, 1]);palette.save(tmp_path/"palette.png")
    np.testing.assert_allclose(image_depth(tmp_path/"palette.png"), [[.2126, .7152]], atol=1e-7)


def test_color_float_pfm_preserves_range_and_channel_order(tmp_path):
    import cv2
    path = tmp_path/"depth.pfm"
    assert cv2.imwrite(str(path), np.array([[[0, 0, 4], [0, 2, 0], [3, 0, 0]]], np.float32))
    np.testing.assert_allclose(image_depth(path), [[4*.2126, 2*.7152, 3*.0722]], atol=1e-6)
