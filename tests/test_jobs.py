import time

import numpy as np
from PIL import Image

from stereodepth_worker.cache import Cache, read_depth
from stereodepth_worker.jobs import Jobs


def media_pair(tmp_path, frames=4):
    color, depth = tmp_path / "color", tmp_path / "depth"
    color.mkdir(); depth.mkdir()
    for i in range(frames):
        Image.new("RGB", (32, 24), (40 + i, 80, 100)).save(color / f"c{i:04d}.png")
        Image.fromarray(np.full((24, 32), 1000 + i * 1000, dtype=np.uint16)).save(depth / f"d{i:04d}.png")
    return str(color / "c0000.png"), str(depth / "d0000.png")


def finish(jobs, request):
    job = jobs.submit(request)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        state = jobs.status(job)
        if state["state"] in ("complete", "failed", "cancelled") and jobs.active is None:
            return state
        time.sleep(.02)
    raise AssertionError("Job timed out")


def test_override_bypasses_ai_and_synchronizes(tmp_path):
    color, depth = media_pair(tmp_path)
    def forbidden(_):
        raise AssertionError("AI was initialized during override")
    jobs = Jobs(Cache(tmp_path / "cache"), forbidden)
    request = dict(source=color, depth_override=True, depth_source=depth, fps="30000/1001")
    result = finish(jobs, request)
    assert result["state"] == "complete", result
    assert result["completed"] == 4
    assert result["fps"] == "30000/1001"
    for i in range(4):
        assert read_depth(jobs.cache.frame(result["key"], i))[0, 0] == np.float32((1000 + i * 1000) / 65535)
    repeated = finish(jobs, request)
    assert repeated["slot"] == result["slot"]
    assert repeated["key"] == result["key"]


def test_short_depth_fails_instead_of_holding(tmp_path):
    color, depth = media_pair(tmp_path)
    from pathlib import Path
    (Path(depth).parent / "d0003.png").unlink()
    jobs = Jobs(Cache(tmp_path / "cache"))
    result = finish(jobs, dict(source=color, depth_override=True, depth_source=depth, fps="24"))
    assert result["state"] == "failed"
    assert "shorter" in result["error"]
    assert not list((jobs.cache.root / "bindings").iterdir())


def test_inference_reuse_and_temporal_separate_cache(tmp_path):
    color, _ = media_pair(tmp_path)
    class Provider:
        key = "test-provider-v1"
        calls = 0
        def infer(self, rgb):
            self.calls += 1
            return np.tile(np.linspace(0, 1, 32, dtype=np.float32), (24, 1))
    provider = Provider()
    jobs = Jobs(Cache(tmp_path / "cache"), lambda _: provider)
    request = dict(source=color, model="test", fps="24")
    raw = finish(jobs, request)
    assert raw["state"] == "complete", raw
    assert provider.calls == 4
    cached = finish(jobs, request)
    assert cached["key"] == raw["key"] and provider.calls == 4
    temporal = finish(jobs, {**request, "temporal": {"enabled": True}})
    assert temporal["state"] == "complete", temporal
    assert temporal["key"] != raw["key"] and temporal["raw_key"] == raw["key"]
    assert provider.calls == 4
    provider.key = "test-provider-v2"
    changed = finish(jobs, request)
    assert changed["key"] != raw["key"] and provider.calls == 8


def test_stateful_video_cache_replays_partial_context(tmp_path):
    color, _ = media_pair(tmp_path)
    class Stateful:
        key = "stateful-test-v1"
        stateful = True
        total_calls = 0
        def __init__(self):
            self.index = 0
        def reset(self):
            self.index = 0
        def infer(self, rgb):
            self.index += 1
            Stateful.total_calls += 1
            return np.full((24, 32), self.index, dtype=np.float32)
    jobs = Jobs(Cache(tmp_path / "cache"), lambda _: Stateful())
    request = dict(source=color, model="test", fps="24")
    first = finish(jobs, request)
    assert first["state"] == "complete", first
    assert Stateful.total_calls == 4
    second = finish(jobs, request)
    assert second["state"] == "complete" and Stateful.total_calls == 4
    jobs.cache.frame(first["key"], 2).unlink()
    third = finish(jobs, request)
    assert third["state"] == "complete", third
    assert Stateful.total_calls == 8
    assert read_depth(jobs.cache.frame(first["key"], 2))[0, 0] == 3
