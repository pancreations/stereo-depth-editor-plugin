# Hardware compatibility and source preview setup

This is a source preview, not a signed production installer. The table distinguishes implemented paths from hardware validation. macOS work must be built and tested on a Mac before publishing a macOS binary.

| Platform | AI depth | Native stereo | Validation in this workspace |
|---|---|---|---|
| Windows x64, RTX 30 series (Ampere) | ONNX CUDA / PyTorch cu128 | CUDA NVRTC | Device-selection tests; physical card test pending |
| Windows x64, RTX 40 series (Ada) | ONNX CUDA / PyTorch cu128 | CUDA NVRTC | Device-selection tests; physical card test pending |
| Windows x64, RTX 50 series (Blackwell) | ONNX CUDA / PyTorch cu128 | CUDA NVRTC | RTX 5070 Ti native parity passed; see validation record |
| Apple silicon, native arm64, macOS 14+ | ONNX CoreML / PyTorch MPS, FP32 | CPU reference pipeline | Implemented experimental paths; Mac build, model and Premiere tests pending |
| CPU | ONNX CPU / PyTorch CPU | CPU reference pipeline | Worker and core regression coverage |

There is no native Metal stereo renderer yet. Apple silicon accelerates AI through CoreML/MPS; stereo reprojection and repair run on CPU. The separate Windows SBS Preview transmitter/window is unavailable on macOS. Use Premiere's Program Monitor and Depth Map/eye/SBS effect previews there. Linux supports the worker and standalone CPU core, not a Premiere plugin.

## NVIDIA setup

Use an NVIDIA driver compatible with CUDA 12.8. The release baseline is PyTorch **2.7.1 + cu128**, torchvision **0.22.1**, and ONNX Runtime GPU **1.23.2**. Blackwell support and cu128 wheels were introduced in [PyTorch 2.7](https://pytorch.org/blog/pytorch-2-7/); [CUDA 12.8](https://developer.nvidia.com/blog/cuda-toolkit-12-8-delivers-nvidia-blackwell-support) adds Blackwell toolkit support. This same package set targets RTX 30/40/50; older CUDA PyTorch environments should be replaced for RTX 50.

From a fresh checkout, using Python 3.11 (3.10–3.13 supported by the setup helper):

```powershell
py -3.11 tools/setup_worker.py --backend cuda
.\scripts\start-worker.ps1 -Device cuda
```

The helper keeps the `.venv310` directory name for compatibility with the existing native worker launcher; the name does not require Python 3.10. `start-worker.ps1 -Setup -Device cuda` uses the Windows Python 3.10 launcher for existing users. Setup installs video/DA3/test extras and pins the GPU-facing dependencies through `constraints/cuda.txt`. This is a baseline constraint set, not a lock of every transitive dependency. CPU setup uses `--backend cpu`. Do not install `onnxruntime` and `onnxruntime-gpu` in the same environment: they share the same Python module. The setup helper rejects that conflict instead of overwriting packages.

Build with `scripts/build.ps1` and your licensed Adobe SDKs, as described in [build instructions](BUILD_AND_INSTALL.md). Native stereo finds NVRTC and its builtins in the worker's PyTorch installation; no system CUDA toolkit is necessary for this path. The experimental `-Cuda` build requires CUDA Toolkit 12.8+ and defaults to architectures `86;89;120`. Override `CMAKE_CUDA_ARCHITECTURES` when building a narrower target set. Runtime compilation queries both the actual GPU and NVRTC's supported targets instead of assuming one GPU architecture.

## Apple silicon setup

Use native arm64 Python, a native arm64 Premiere installation, macOS 14+, CMake 3.24+, and Xcode tools with `Rez`. Avoid Rosetta. The conservative OS baseline is for this source preview's dependency set.

```bash
python3.11 tools/setup_worker.py --backend apple
bash scripts/start-worker.sh --device auto
```

`auto` selects CoreML for ONNX and MPS for PyTorch when available. `mps` also maps to CoreML for an ONNX model. `coreml` is an ONNX-only selection; use `auto` or `mps` when switching between model families. PyTorch MPS uses FP32 here, with CUDA-only mixed precision disabled. See the official [MPS backend](https://docs.pytorch.org/docs/stable/notes/mps.html) and [CoreML execution provider](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html) documentation.

CoreML may assign unsupported ONNX graph nodes to CPU, and dynamic-shape/FP16 graphs need model-specific validation. Start with **DA-V2 Small FP32**, Preview or Balanced quality. An available backend is not evidence that every model operator runs on it. If a PyTorch model reports an unsupported MPS operation, select CPU, or explicitly opt into PyTorch's operation fallback before starting the worker with `PYTORCH_ENABLE_MPS_FALLBACK=1`. The worker does not automatically retry failed GPU model inference on CPU.

Build the SDK-free core, macOS worker bridge, and HTTP tests:

```bash
bash scripts/build-macos.sh
S3D_HTTP_TEST=build-macos/s3d_http_tests .venv310/bin/python -m pytest -q
.venv310/bin/python tools/demo.py --renderer build-macos/s3d_render
```

To build the experimental Premiere bundle, obtain the **macOS** Adobe SDKs and set both roots:

```bash
export AE_SDK_ROOT="/path/to/AfterEffectsSDK"
export PREMIERE_SDK_ROOT="/path/to/PremiereSDK"
bash scripts/build-macos.sh
bash scripts/install-native-macos.sh
```

The bundle uses an arm64 PiPL entry point, AppKit depth picker, a cancellable libcurl connection to the local worker, and the CPU stereo engine. It installs into the user's Adobe MediaCore directory. The macOS Adobe SDK compilation, bundle discovery, native dialogs, Premiere renders and AME exports remain unverified. Distributed bundles will need signing/notarization and install/uninstall QA. No macOS binary is included in this Windows-built source preview.

## Worker configuration and relocation

`--device` sets automatic render inference and the default for offline jobs; an explicit device in a job overrides it. The panel's inference selector applies to its offline jobs. Change automatic native rendering with the worker launch option or `S3D_DEVICE`, then restart the worker. Explicit unavailable devices fail with an error; `auto` may choose CPU when no supported accelerator is available. ONNX provider initialization failures do not silently become CPU sessions. Cache generations include the resolved backend, so CPU/CUDA/CoreML results do not collide.

| Setting | Purpose |
|---|---|
| `S3D_HOME` | Checkout/runtime root for native worker launch, models and kernel source. Overrides the release's adjacent `StereoDepth.runtime.txt`. Development builds fall back to the build checkout; release builds never do. |
| `S3D_PYTHON` | Full worker Python executable path, if using a different environment. |
| `S3D_DEVICE` | Worker default: `auto`, `cuda`, `mps`, `coreml`, or `cpu`. |
| `S3D_CACHE_ROOT` | Shared native/worker cache path. |
| `S3D_NVRTC_DIR` | Windows directory containing a matched NVRTC/builtins pair, if outside PyTorch. |
| `S3D_STEREO_DEVICE=cpu` | Explicit Windows native CPU stereo rendering; separate from AI device selection. |

Set native settings in the environment inherited by Premiere/AME **before launching them**. Shell exports do not affect an already-running host or necessarily a Finder-launched application. Keeping the build checkout in place needs no root override. Moving only the `.prm`/`.plugin` is not a standalone runtime installation; retain the worker, model files and CUDA source. Keep model repository paths relative; regenerate source-hash manifests when source contents/line endings change.

Default cache paths are `%LOCALAPPDATA%/StereoDepth/cache` on Windows and `~/Library/Caches/StereoDepth/cache` on macOS. `runtime/connection.json` contains a local authentication token and must never be included in a release.

## Models and acceptance tests

Weights and upstream source repositories are deliberately excluded from the release archive. Follow [model setup](../models/README.md). DA-V2 requires an exported/registered ONNX model; VDA and DA3 require their official source checkout and weights. The default Large FP16 model is not bundled or automatically exported on a fresh checkout. For a portable first test, export Small FP32 and select it explicitly.

On each target machine, run worker tests, the external-depth demo, real-model inference, and native CPU/CUDA parity where applicable. Record driver, OS, Python/runtime versions, GPU, model, resolution, memory and timing. Exercise Preview/Balanced before large models or Maximum quality; model memory varies substantially. Complete [Premiere host acceptance](RELEASE_CHECKLIST.md) before calling a binary production-ready.
