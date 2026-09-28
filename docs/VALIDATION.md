# Validation performed in this workspace

Date: 2026-09-27. Windows x64, MSVC 19.44, CMake 4.2.3, Python 3.10. Native code built in Release configuration. The exact Python development environment is recorded in `development-environment.txt`; it includes export/test packages that do not belong in the deployed worker.

## Passed

- CMake configure and Release build of `stereodepth.lib`, `s3d_render.exe` and `s3d_tests.exe`.
- Native `StereoDepth.prm` compiled with the supplied Premiere Pro 26.0 and shared effect 26.5 SDKs. Fixed Premiere support-header selection and const suite acquisition, removed redundant platform defines, and cleared each parameter definition before registration.
- Two CTest suites pass: core and `premiere_adapter`. The adapter test loads the actual DLL, locates its PiPL resource and `EffectMain`, exercises Premiere-only global setup and float pixel format registration, checks 45 parameter entries including groups, Invert Depth Map and native external Browse, verifies explicit Original float/HDR passthrough and verifies missing external sources fail with a Browse instruction using mock SDK callbacks. This does not substitute for real Premiere rendering.
- Native installation target: `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\StereoDepth\StereoDepth.prm`. The 0.2 installer verifies built/installed hashes after elevation and writes `artifacts/v02-install.json` on success; until that record exists the previous installed build remains active.
- CTest native suite covers float/HDR/negative color identity, parallax limits and direction, convergence plane, foreground visibility, fast/content-aware repair coverage, injectable inpainting-provider boundaries, retained masks, eye order, arbitrary/portrait/odd sizes, remapping, temporal cut reset, rational time and invalid input rejection.
- 24 Python tests passed. They cover external bypass, exact source/depth order, 16-bit PNG precision, float metric EXR channel precision, float TIFF, lossless 16-bit FFV1 video decoding, corrupt/truncated caches, cache isolation and binding stability, model invalidation, temporal reuse, stateful model replay after partial cache loss, real ONNX runtime I/O on a small contract graph, model checksums, HTTP authentication, model catalogue coverage/default, incomplete-download rejection, registration and temporal motion/cut rejection.
- End-to-end external PNG sequence → float cache → native C++ renderer. Output artifacts are `artifacts/demo/half-sbs.png` at 320×180 and `full-sbs.png` at 640×180, plus float PFM outputs.
- End-to-end **real Depth Anything V2 Small ONNX inference + temporal stabilization** → float cache → native C++ renderer. Outputs under `artifacts/ai-demo/` in both SBS sizes.
- End-to-end **real Video Depth Anything Small streaming PyTorch inference + flow stabilization** → float cache → native C++ renderer, four ordered frames on CPU. Outputs under `artifacts/video-ai-demo/` in both SBS sizes. This exercises model state transitions but is not a long-video or RTX stress test.
- The user's existing DA-V2 Small FP16 export also passed an AI → temporal → cache → C++ SBS demo under `artifacts/user-model-demo/`, using explicitly configured basic ORT graph optimization to avoid a CPU FP16 fusion failure.
- All three user-supplied FP16 ONNX models (Small/Base/Large) passed CPU inference at landscape and portrait dimensions. Each project copy matches its original SHA-256.
- DA3 Mono Large official safetensors passed four-frame CPU inference + flow stabilization + float cache + both native SBS formats under `artifacts/da3-mono-demo/`. This verifies the distance conversion and integration path, not comparative model quality or long-video temporal stability.
- DA3 Mono Large also passed the four-frame pipeline on **RTX 5070 Ti, 16 GB, driver 616.64**, PyTorch 2.7.1+cu128, Balanced preset, under `artifacts/da3-rtx-demo/`. Single-image GPU smoke checks at Balanced and Maximum returned finite 2048×1362 depth. First-inference times were 1.53 s / 1.57 s; PyTorch peak allocated memory was 2147 / 2988 MiB. These exclude model loading, include startup effects, and are neither sustained FPS nor total GPU memory measurements. Records: `artifacts/da3-rtx-validation/result.json` and `artifacts/da3-rtx-maximum/result.json`.
- User DA-V2 Large FP16 passed four-frame CUDA inference + flow stabilization + float cache + native SBS using ONNX Runtime GPU 1.23.2, Balanced preset, under `artifacts/dav2-large-rtx-demo/`. ORT reports some CPU-assigned nodes; this is a successful CUDA-provider run, not proof of an entirely GPU-resident graph.
- Invert Depth Map flips the processed grayscale preview, preserves the midpoint and raw depth, and composes with Black = Near interpretation in the native regression suite.
- JavaScript syntax checks for the UXP panel and Premiere API wrapper.
- `pip check`: no broken dependency requirements.

## Real model export

The first naive export froze square positional encoding and failed landscape inference. The exporter now replaces positional interpolation with dynamic size-based bicubic interpolation and preserves dynamic dimensions in the DPT head. This is an explicit export variant; it changes upstream's offset-based positional sampling slightly. It is not claimed to be numerically identical to unmodified upstream inference.

Exported ONNX matches the **adapted PyTorch model** at these shapes:

| Input H×W | Maximum absolute depth difference |
|---|---:|
| 154×280 | 0.00001192 |
| 280×154 | 0.00000572 |
| 518×518 | 0.00000334 |

Checksums and comparison results are in `models/depth_anything_v2_vits.json`. The graph and checkpoint exist locally but are ignored by Git. Model export does not publish the manifest until alternate-shape ONNX validation succeeds.

## Real model execution

One upstream example image (`assets/examples/demo01.jpg`, 2048×1362), ONNX Runtime CPU provider, two intra-op threads, one measured inference per preset. These are smoke-test timings, not reliable performance benchmarks or RTX measurements. They include resize/preprocessing and output upsampling, exclude session initialization, and do not measure Premiere rendering.

| Preset | Time |
|---|---:|
| Preview | 0.085 s |
| Balanced | 0.281 s |
| High | 0.709 s |
| Maximum | 2.137 s |

All outputs had the original image dimensions and finite float values. Depth previews and measured results are under `artifacts/model-validation/`.

## Not verified

`StereoDepth.prm` is compiled and installed, and loads in a mock SDK harness; real Premiere discovery/render/export verification is pending. The 0.2 CCX installed successfully through Adobe UPIA, but the panel has not been exercised in Premiere. Native NVRTC CUDA compilation and DLL rendering passed; PrGPUFilter integration, remaining checkpoints' RTX validation, host zero-copy frame access, video-quality/comfort evaluation, HDR export, full-SBS host sizing, source-time mapping and release packaging remain open. Native CUDA processing is exercised through mock CPU-frame callbacks, not Premiere GPU device suites.

See `RELEASE_CHECKLIST.md` for the complete acceptance matrix.


## Version 0.2 render-path checks

- Worker regression suite: 26 passed. Native CTest: core and Premiere adapter passed.
- RTX 5070 Ti NVRTC test: seven processed previews plus inversion matched the CPU reference at the tested full-size depth resolution; warm 1920x1080 stereo averaged 10.44 ms/frame over 12 frames including color upload/download. Excludes AI, IPC and Premiere.
- Actual `.prm` through mocked Adobe callbacks at 1624x1080: cached automatic depth preview 31.58 ms, Half SBS 29.64 ms; direct external PNG depth preview 48.54 ms, Half SBS 47.26 ms. These are individual warm samples, not sustained Premiere FPS. First cached request including renderer initialization took 266/227 ms respectively.
- Earlier uncached DA-V2 Large Balanced inference through the same DLL took 3.33 seconds including model initialization. No real-time inference claim is made.
- Depth and SBS artifacts: `artifacts/final-auto-test/` and `artifacts/final-external-test/`. Earlier PNG visualizations were inspected and show grayscale scene depth and paired compressed eye views.
- Native file dialog interaction, Premiere preview invalidation/playback, Render Effects cache behavior in the real host, UXP runtime behavior and exports remain unverified.
