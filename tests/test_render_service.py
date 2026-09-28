import json
from fractions import Fraction
import numpy as np
import pytest
from PIL import Image

from stereodepth_worker.render_service import RenderService, REQUEST, RESPONSE


def test_decoder_eviction_and_backward_seek_close_containers(tmp_path):
    closed = []
    class FakeMedia:
        def __init__(self, name): self.name = name
        def frames(self):
            try:
                while True: yield None
            finally:
                closed.append(self.name)
    service = RenderService(tmp_path, tmp_path)
    for name in ('one', 'two', 'three'):
        decoder = service.decoder(name, FakeMedia(name), 0)
        next(decoder[1]); decoder[0] = 10
    assert closed == ['one'] and len(service.external_decoders) == 2
    decoder = service.decoder('three', FakeMedia('three-reopened'), 0)
    assert closed == ['one', 'three'] and decoder[0] == 0
    assert len(service.external_decoders) == 2


def packet(rgb, model=2, quality=1, source="clip.mov"):
    name=source.encode();h,w=rgb.shape[:2]
    return REQUEST.pack(b"SDR1",w,h,model,quality,len(name))+name+rgb.tobytes()


def test_render_backend_reaches_factory_and_separates_cache(tmp_path, monkeypatch):
    manifest = tmp_path / "depth-anything-v2-large-fp16.json"
    (tmp_path / "large.onnx").write_bytes(b"weights")
    manifest.write_text(json.dumps({"file": "large.onnx", "contract": "relative-inverse-depth-nchw-v1"}))
    monkeypatch.setattr("stereodepth_worker.render_service.resolve_device", lambda value, **kw: value)
    devices = []
    class Provider:
        def infer(self, rgb):
            return np.ones(rgb.shape[:2], np.float32)
    def factory(path, quality, device):
        devices.append(device)
        return Provider()
    body = packet(np.zeros((4, 6, 3), np.uint8))
    for device in ("cpu", "cuda", "coreml"):
        service = RenderService(tmp_path, tmp_path, factory, device=device)
        assert RESPONSE.unpack_from(service.frame(body))[-1] == 0
        assert RESPONSE.unpack_from(service.frame(body))[-1] == 1
    assert devices == ["cpu", "cuda", "coreml"]


def test_render_infers_misses_reuses_frames_and_freezes_range(tmp_path):
    models=tmp_path/"models";models.mkdir()
    weights=models/"large.onnx";weights.write_bytes(b"weights")
    manifest=models/"depth-anything-v2-large-fp16.json"
    manifest.write_text(json.dumps({"file":"large.onnx","contract":"relative-inverse-depth-nchw-v1"}))
    calls=[]
    class Provider:
        def infer(self,rgb):
            calls.append(int(rgb[0,0,0]));return rgb[...,0].astype(np.float32)+1
    service=RenderService(tmp_path,models,lambda *args: Provider())
    rgb=np.zeros((4,6,3),np.uint8);rgb[:,3:]=100
    first=service.frame(packet(rgb));second=service.frame(packet(rgb))
    assert len(calls)==1 and RESPONSE.unpack_from(first)[-1]==0 and RESPONSE.unpack_from(second)[-1]==1
    assert first[RESPONSE.size:]==second[RESPONSE.size:]
    rgb+=20;third=service.frame(packet(rgb))
    assert RESPONSE.unpack_from(first)[3:5]==RESPONSE.unpack_from(third)[3:5]
    assert len(calls)==2
    service.frame(packet(rgb,quality=2));assert len(calls)==3
    # A restarted worker can serve existing frames without constructing a model.
    restarted=RenderService(tmp_path,models,lambda *args: (_ for _ in ()).throw(AssertionError("Unexpected model load")))
    assert RESPONSE.unpack_from(restarted.frame(packet(rgb)))[-1]==1


def test_external_picker_registration_and_frame_sync_bypass_inference(tmp_path):
    def forbidden(*args):raise AssertionError("External depth must never construct AI")
    service=RenderService(tmp_path,tmp_path/"models",forbidden)
    for i in range(3):
        Image.fromarray(np.full((4,8),i*20000,np.uint16)).save(tmp_path/f"z{i:04d}.png")
    index=service.register_external(tmp_path/"z0000.png")
    assert service.register_external(tmp_path/"z0000.png")==index
    result=service.external(dict(id=index,time=1001,scale=30000,offset=0,fps_num=30000,fps_den=1001))
    depth=np.frombuffer(result[RESPONSE.size:],dtype="<f4")
    assert np.allclose(depth,20000/65535)
    with pytest.raises(ValueError,match="shorter"):
        service.external(dict(id=index,time=5,scale=1,offset=0,fps_num=30,fps_den=1))
    with pytest.raises(ValueError):service.frame(b"broken")


def test_explicit_sequence_and_still_import_persist_and_seek(tmp_path):
    for frame in range(1001, 1004):
        Image.fromarray(np.full((3, 5), (frame-1000)*10000, np.uint16)).save(tmp_path/f"depth.{frame}.png")
    service = RenderService(tmp_path, tmp_path)
    path = tmp_path/"depth.1002.png"
    sequence = service.register_external(path, "sequence")
    still = service.register_external(path, "still")
    assert sequence != still
    assert service.register_external(path, "sequence") == sequence
    record = json.loads((service.root/"external"/f"{sequence}.json").read_text())
    assert record["frame_count"] == 2
    service = RenderService(tmp_path, tmp_path)  # selection survives worker restart
    for index, frame, expected in [(sequence, 1, 30000), (sequence, 0, 20000), (still, 100, 20000)]:
        result = service.external(dict(id=index, time=frame*1001, scale=30000, offset=0, fps_num=30000, fps_den=1001))
        assert np.allclose(np.frombuffer(result[RESPONSE.size:], dtype="<f4"), expected/65535)
    with pytest.raises(ValueError, match="shorter"):
        service.external(dict(id=sequence, time=2, scale=30, offset=0, fps_num=30, fps_den=1))
    result = service.external(dict(id=sequence, time=0, scale=30, offset=1, fps_num=30, fps_den=1))
    assert np.allclose(np.frombuffer(result[RESPONSE.size:], dtype="<f4"), 30000/65535)


def test_sequence_import_reports_gaps_immediately_but_allows_still(tmp_path):
    for frame in (1, 3):
        Image.fromarray(np.zeros((2, 3), np.uint16)).save(tmp_path/f"z{frame:04d}.png")
    service = RenderService(tmp_path, tmp_path)
    with pytest.raises(ValueError, match="missing frame"):
        service.register_external(tmp_path/"z0001.png", "sequence")
    assert service.register_external(tmp_path/"z0001.png", "still") > 0
    with pytest.raises(ValueError, match="import mode"):
        service.register_external(tmp_path/"z0001.png", "invalid")


def test_rgb_external_sequence_reads_on_demand_without_depth_disk_cache(tmp_path):
    source = tmp_path/"source";source.mkdir()
    for frame, rgb in enumerate([(255, 0, 0), (0, 255, 0), (0, 0, 255)]):
        Image.fromarray(np.full((3, 4, 3), rgb, np.uint8)).save(source/f"depth{frame:04d}.png")
    originals = {p.name: p.read_bytes() for p in source.iterdir()}
    def forbidden(*args): raise AssertionError("RGB external depth must bypass AI")
    service = RenderService(tmp_path/"cache", tmp_path, forbidden)
    index = service.register_external(source/"depth0000.png", "sequence")
    for frame, expected in [(2, .0722), (0, .2126), (1, .7152)]:
        result = service.external(dict(id=index, time=frame, scale=30, offset=0, fps_num=30, fps_den=1))
        np.testing.assert_allclose(np.frombuffer(result[RESPONSE.size:], dtype="<f4"), expected, atol=1e-7)
    assert {p.name: p.read_bytes() for p in source.iterdir()} == originals
    # Only the tiny source registration is persisted; no converted frames.
    assert [p.relative_to(service.root).as_posix() for p in service.root.rglob("*") if p.is_file()] == [f"external/{index}.json"]
