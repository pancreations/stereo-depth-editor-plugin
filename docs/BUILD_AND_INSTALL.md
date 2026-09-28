# Build and install

For the RTX 30/40/50 package baseline, Apple silicon build instructions and runtime relocation, start with [hardware compatibility](HARDWARE_COMPATIBILITY.md). The Windows notes below also contain historical local validation details.

## Requirements

- Windows x64; Visual Studio 2022 with Desktop development with C++, Windows SDK, CMake 3.24+.
- Premiere Pro 25.6+ for the UXP panel. This machine has Premiere 26.5.1; in-host tests are pending.
- Python 3.10+ for the worker; provided scripts use 3.10. PyTorch is needed for model export and the optional VDA/DA3 providers. ONNX Runtime can also preload compatible CUDA/cuDNN DLLs supplied with CUDA PyTorch.
- Premiere Pro C++ SDK and Adobe shared effect SDK for `.prm` compilation.
- CUDA NVRTC/builtins DLLs from the installed CUDA PyTorch environment power native stereo rendering; nvcc is not required. The optional standalone CUDA backend needs the CUDA toolkit. ONNX Runtime CUDA needs a compatible NVIDIA driver and matching CUDA/cuDNN runtime; it does not need nvcc to run.
- Optional OpenImageIO for EXR; ONNX checkpoint/manifest for AI.

## Adobe SDKs and UXP Developer Tools are different

UXP Developer Tools loads and debugs `uxp/manifest.json`. It does not supply `AE_Effect.h`, `PrSDKPixelFormat.h` or `PiPLtool.exe`.

Download the Premiere Pro C++ SDK from [Adobe's Premiere developer page](https://developer.adobe.com/premiere-pro/) and the shared effect SDK from [Adobe's After Effects developer page](https://developer.adobe.com/after-effects/). Accept Adobe's SDK terms yourself. This is a Premiere effect using Adobe's shared binary effect ABI, not an After Effects plugin product.

Expected roots:

```text
AE_SDK_ROOT/Examples/Headers/AE_Effect.h
AE_SDK_ROOT/Examples/Util/Param_Utils.h
AE_SDK_ROOT/Examples/Resources/PiPLtool.exe
PREMIERE_SDK_ROOT/Examples/Headers/PrSDKPixelFormat.h
```

Roots with `Headers`, `Util`, and `Resources` directly underneath are supported too.

```powershell
.\scripts\build.ps1 -AESdk 'D:\SDKs\AfterEffectsSDK' -PremiereSdk 'D:\SDKs\PremiereSDK'
```

The adapter is excluded from the SDK-free CMake configuration. `scripts/build.ps1` now detects the supplied workspace folders `AfterEffectsSDK_26.5_win` and `Premiere Pro 26.0 C++ SDK` automatically. The Release `.prm` successfully compiles against these SDKs and passes the DLL/SDK callback smoke test. Run the host checklist before treating it as a release build.

## Native installation after successful SDK build

Close Premiere and Adobe Media Encoder. Run an elevated PowerShell:

```powershell
.\scripts\install-native.ps1
```

This copies the built `StereoDepth.prm` to:

```text
C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\StereoDepth\StereoDepth.prm
```

Restart Premiere. Find **Effects → StereoDepth → 2D to 3D**. If it is absent, inspect Premiere's plugin-loading log and use the SDK debug instructions. To uninstall this development build, close both hosts and remove only the verified `StereoDepth.prm` and `StereoDepthPreview.prm` files. Never delete the whole folder: it may contain backups or unrelated files. The development installer backs up an existing binary as `.previous`. The release ZIP has a separate hash-checked uninstaller; see [safe removal](WINDOWS_ZIP.md).

The compiled development binary is `build/native/premiere/Release/StereoDepth.prm`. Installation requires Windows administrator elevation. Verify the installed and built SHA-256 hashes match, then restart Premiere. In version 0.2, cache slot zero generates/reuses AI depth automatically or loads the selected external depth. Only the explicit Original preview passes through the original frame. The optional prepared-cache workflow is needed for Video Depth Anything and offline temporal stabilization.

## Start the depth worker

```powershell
.\scripts\start-worker.ps1 -Setup -Device cpu
```

For RTX, install the CUDA extra in a separate environment or remove the CPU ONNX Runtime package first; avoid installing `onnxruntime` and `onnxruntime-gpu` together since they share a Python module.

```powershell
.\.venv310\Scripts\python.exe -m pip uninstall onnxruntime
.\.venv310\Scripts\python.exe -m pip install -e ".[cuda,exr]"
.\scripts\start-worker.ps1 -Device cuda
```

Check the actual ONNX Runtime/CUDA/cuDNN version matrix in [the official provider documentation](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html). The worker refuses to label a silently CPU-fallback session as CUDA. CPU nodes within a CUDA graph can still exist; profile model placement before claiming all-GPU inference.

This workspace now has ONNX Runtime GPU 1.23.2 and PyTorch 2.7.1+cu128 / torchvision 0.22.1+cu128 installed in `.venv310`, replacing CPU-only packages. DA3 Mono Large GPU inference was exercised on the RTX 5070 Ti. For a fresh equivalent setup, install the optional `video,da3` extras and that matching PyTorch pair from the CUDA 12.8 index. Use [PyTorch's installation selector](https://pytorch.org/get-started/locally/) when changing versions/hardware. Do not run `-Setup -Device cpu` in this environment unless intentionally switching back to the CPU package set.

The foreground worker creates `runtime/connection.json`. Keep this local: it contains the token that authorizes local filesystem jobs. The server binds only `127.0.0.1`, uses no cloud service, and has no arbitrary command-execution endpoint. Ctrl+C stops it. The connection token rotates at restart.

Default cache root: `%LOCALAPPDATA%\StereoDepth\cache`.

To use a different cache location, set `S3D_CACHE_ROOT` for Premiere/Media Encoder **before launching them**, and start the worker with the same path:

```powershell
$env:S3D_CACHE_ROOT = 'N:\StereoDepthCache'
.\.venv310\Scripts\python.exe -m stereodepth_worker.server --cache $env:S3D_CACHE_ROOT
```

The panel shows the worker's cache location. It does not change a running native host's environment. Cache directories and bindings must move together.

## Install the packaged UXP panel

Run `python tools/package_panel.py`, then install `artifacts/StereoDepth-premierepro.ccx` with Adobe UnifiedPluginInstallerAgent or Creative Cloud. The local package was installed successfully with UPIA. Open **Window > UXP Plugins > 2D to 3D**. Connect using `runtime/connection.json` when managing downloads or prepared video caches. Direct external browsing and automatic DA-V2/DA3 rendering are native controls and do not require opening the panel.

The development native binary uses the configured workspace path for Python, models and kernel source. Keep that directory intact. A relocatable runtime installer remains release work.

## Load the UXP panel for development

1. Enable Premiere's UXP developer mode following [Adobe's UXP setup guide](https://developer.adobe.com/premiere-pro/uxp/introduction/).
2. Open Adobe UXP Developer Tools, choose Add Plugin, select `uxp/manifest.json` from your checkout, then Load into Premiere.
3. Open the **2D to 3D** panel and click **Connect local worker…**. Select `runtime/connection.json`.
4. Generate a cache. Apply / Bind works only after the native effect is installed. The panel reports missing native binaries explicitly.

A local CCX is packaged; signing, Marketplace submission and an installer bundling runtime DLLs are release work. Neither UXP Developer Tools nor this panel replaces the native effect binary.

## CUDA development

```powershell
cmake -S . -B build-cuda -G "Visual Studio 17 2022" -A x64 -DS3D_BUILD_CUDA=ON
cmake --build build-cuda --config Release
```

`native/gpu/cuda_backend.cu` requires caller-owned pitched device buffers and a host stream. It does not allocate/copy source frames. Its GPU visibility tie behavior differs slightly from CPU epsilon comparisons and its bounded repair differs from CPU whole-row extension. Establish tolerances and parity tests before enabling it in a host renderer. Add `PrGPUFilter` registration/device suites using the matching SDK sample; the current `.prm` uses the NVRTC CUDA renderer through CPU host frames, with explicit upload/download. High repair runs on CUDA starting in 0.3.1; the standalone CPU reference remains available for comparison.
