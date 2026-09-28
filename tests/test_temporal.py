import numpy as np
from stereodepth_worker.temporal import Temporal, stabilize


def test_scene_cut_has_no_ghosting():
    current = np.ones((24, 32), dtype=np.float32)
    previous = np.zeros_like(current)
    white = np.full((24, 32, 3), 255, dtype=np.uint8)
    black = np.zeros_like(white)
    np.testing.assert_array_equal(stabilize(current, previous, white, black, Temporal(enabled=True)), current)


def test_static_noise_is_reduced_without_blending_new_foreground():
    rng = np.random.default_rng(7)
    previous = np.full((48, 64), .5, dtype=np.float32)
    current = previous + rng.normal(0, .005, previous.shape).astype(np.float32)
    rgb = np.full((48, 64, 3), 100, dtype=np.uint8)
    result = stabilize(current, previous, rgb, rgb, Temporal(enabled=True))
    assert np.std(result[2:-2, 2:-2]) < np.std(current[2:-2, 2:-2])
    current[16:32, 20:44] = 1
    result = stabilize(current, previous, rgb, rgb, Temporal(enabled=True))
    assert result[24, 32] > .99
