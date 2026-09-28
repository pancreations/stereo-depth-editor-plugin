# Compatibility validation — 2026-09-28

Local host: Windows x64, NVIDIA GeForce RTX 5070 Ti, Python 3.10 environment, PyTorch 2.7.1+cu128, ONNX Runtime GPU 1.23.2. These are local development checks; none are a substitute for Premiere/AME acceptance testing.

| Check | Result |
|---|---|
| Python regression suite | 57 passed, including device routing, backend cache isolation, unavailable-backend errors, real CPU ONNX inference and HTTP cancellation |
| Windows SDK build | StereoDepth.prm, preview component, adapter and HTTP test binaries built successfully |
| Native core / buffer ownership / Premiere callback smoke test | Passed |
| Standalone SDK-free configure/build | Passed in a fresh build directory; 2/2 CTest tests passed |
| External depth → worker cache → standalone native Half/Full SBS | Passed, outputs 320×180 and 640×180 |
| DA-V2 Large FP16, CUDA, Preview → temporal cache → native Half/Full SBS | Passed on four generated frames |
| DA3 Mono Large, CUDA, Preview | Finite 1362×2048 output; single-image inference ~1.52 s; peak allocated ~1955 MiB |
| Video Depth Anything Small, CUDA, Preview | Finite 1362×2048 output; single-image inference ~0.44 s; peak allocated ~212 MiB |
| Native CUDA preview parity / inversion / High repair parity | Passed; High repair maximum difference 0 in the test scene |
| Native CUDA cached-depth 1080p timing | ~10.91 ms/frame; High repair ~10.10 ms/frame versus CPU ~164.69 ms/frame |
| Windows SBS desktop capture test | Failed: capture returned white sample pixels instead of the transmitted test frame |
| Apple silicon, RTX 30/40 physical hardware | Not available for testing in this workspace |
| macOS SDK build / CoreML and MPS real models / in-host rendering | Not run |
| GitHub Actions | Workflow added; not run remotely |

Native timings include CPU/GPU frame transfer but exclude AI inference, model startup and Premiere. Torch timings are single-image smoke tests with Preview-sized analysis and output resized to the original image. They are not sustained playback benchmarks. ONNX reported that some nodes were assigned to CPU, as is permitted within an accelerator session.

The desktop-capture failure leaves the full Windows CTest run at 3/4 passed. It must be investigated on an interactive desktop before claiming that component passed. No preview implementation was changed as part of this compatibility work. An obsolete Python test expecting RGB depth imports to be rejected was corrected to match the existing documented intensity conversion and the newer media tests.

Reproduction commands:

```powershell
.\.venv310\Scripts\python.exe -m pytest -q
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\native\premiere\Release\s3d_gpu_smoke.exe
.\.venv310\Scripts\python.exe tools/demo.py --model models/depth-anything-v2-large-fp16.json --device cuda --output artifacts/compatibility/onnx
.\.venv310\Scripts\python.exe tools/validate_torch_model.py --model models/da3mono-large.json --quality Preview --device cuda --output artifacts/compatibility/da3
.\.venv310\Scripts\python.exe tools/validate_torch_model.py --model models/video_depth_anything_vits.json --quality Preview --device cuda --output artifacts/compatibility/vda
```

The real-model commands require locally installed model weights/manifests and the upstream repositories. Results and image artifacts are under `artifacts/compatibility/`; they are excluded from the source archive. Follow the [hardware guide](HARDWARE_COMPATIBILITY.md) for fresh setup and the [release checklist](RELEASE_CHECKLIST.md) for remaining gates.
