# Stereo Depth Editor Plugin

A Premiere-specific development project with a tested native stereo engine, local depth worker, UXP companion panel, and SDK-dependent native clip-effect adapter.

**Status: unsigned Windows preview.** Download the Windows ZIP from Releases and read [installation and safe removal](docs/WINDOWS_ZIP.md). It includes the two native plug-ins, optional copy-based install/uninstall scripts, worker source and companion panel. Python, AI weights and GPU dependencies require separate setup. NVIDIA RTX 30/40/50 setup targets CUDA 12.8; Apple silicon source support is experimental. RTX 5070 Ti is locally tested; other hardware, macOS builds, and in-Premiere production validation remain pending.

Start with [hardware setup and compatibility](docs/HARDWARE_COMPATIBILITY.md), [source release preparation](docs/GITHUB_RELEASE.md), and [current validation](docs/COMPATIBILITY_VALIDATION.md). The [release gates](docs/RELEASE_CHECKLIST.md) still apply. AI inpainting and native Metal stereo rendering are not implemented.

## Using version 0.2

Restart Premiere after installation. Apply **StereoDepth > 2D to 3D**. Under **DEPTH SOURCE**, click **Browse Depth Source...** to select an external image sequence or depth video; selection enables override automatically. The control label displays the selected filename. **OUTPUT AND PREVIEW > Preview > Depth Map** displays processed depth; **Final Stereo (SBS)** displays the packed eyes. **DEPTH MAP > Invert Depth Map** reverses the map.

With override off and optional Prepared Cache Slot at zero, the effect generates missing DA-V2/DA3 depth as Premiere requests frames and reuses it on subsequent renders. No separate generate step is required for these models. First inference/model startup can take seconds. Video Depth Anything and flow stabilization still require ordered cache preparation in the companion panel. The panel is packaged as `artifacts/StereoDepth-premierepro.ccx`; open it through **Window > UXP Plugins > 2D to 3D** after installation.

The native build depends on its checkout, worker environment and local models. Windows CUDA stereo also needs the kernel source. Keep them together; see the hardware guide for `S3D_HOME` and `S3D_PYTHON` relocation settings. Canvas Fit offers Fit (show whole image) and Fill (crop edges) in both SBS modes. Full-resolution Full SBS needs a double-width sequence/canvas. Premiere playback, file-dialog behavior and exports must be verified in the real host before production use.

## Start here

- [0.3.1 memory fixes and crash-investigation limits](docs/MEMORY_FIXES_0.3.1.md)
- [SBS Preview window: installation, capture and controls](docs/SBS_PREVIEW.md)
- [Build and Premiere installation](docs/BUILD_AND_INSTALL.md)
- [Editor workflow, external depth, and Full SBS](docs/WORKFLOW.md)
- [Architecture and cache specification](docs/ARCHITECTURE.md)
- [Model setup and licensing](models/README.md)
- [DA3 comparison and existing user models](docs/MODEL_TRADEOFFS.md)
- [Validation results](docs/VALIDATION.md)
- [Implementation status and remaining release work](docs/RELEASE_CHECKLIST.md)

## What runs now

- C++20 float RGBA stereo processing: depth fidelity/filtering, contrast/gamma, near/far remapping, normalization with a fixed clip range, inversion, offset and scale.
- Visibility-aware forward reprojection with subpixel splats, stereo strength, convergence, eye separation, pair-parallax limiting and direction reversal.
- Fast gap stretching, background-aware edge extension and bounded exemplar content-aware repair, with an original-camera fallback. No newly introduced black/transparent holes for opaque input.
- Half SBS and Full SBS, either eye order, arbitrary image aspect ratios, all eight requested diagnostic previews.
- External grayscale PNG/TIFF/PFM sequences, optional float EXR channels through OpenImageIO, and depth videos decoded by PyAV. External override never constructs an AI provider.
- Explicit model picker with 17 entries and on-demand VDA/DA3 downloads. DA-V2 Large is the default; Small/Base/Large ONNX, VDA relative/metric streaming variants, and the DA3 family remain selectable independently of inference resolution. DA3 currently uses single-view inference, with optional flow stabilization.
- Atomic, checksummed float depth caching, content-derived identity, immutable numeric bindings, cancellation, and optical-flow-based offline stabilization.
- UXP panel source for worker connection, media/model browsing, depth generation, status/cancel/clear, selection lookup, and undoable native-effect binding.

## Local build and demo

This workspace already has a configured CUDA worker environment. Run `scripts/start-worker.ps1` without `-Setup` to use it. The fresh CPU setup below is for a separate checkout/environment; do not install CPU and GPU ONNX Runtime together. See the build guide for RTX setup.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

py -3.10 -m venv .venv310
.\.venv310\Scripts\python.exe -m pip install -e ".[cpu,test]"
.\.venv310\Scripts\python.exe -m pytest -q
.\.venv310\Scripts\python.exe tools/demo.py
```

The demo sends a numbered 16-bit PNG depth sequence through the worker cache, reads that cache in C++, and writes `artifacts/demo/half-sbs.png` (320×180) and `full-sbs.png` (640×180). The accompanying PFM files retain floating-point RGB. This validates the processing path without pretending to be a Premiere render.

## Repository layout

```text
native/include/s3d/   host-independent frame types and processing contracts
native/src/           depth, temporal, reprojection, repair, packing, cache I/O
native/premiere/      Premiere-only effect entry point, parameters, PiPL, build
native/gpu/           NVRTC CUDA renderer, reusable buffers, parity tests
native/tests/         native regression tests
native/tools/         standalone float-image renderer
worker/               local AI/media/cache/temporal jobs and authenticated API
uxp/                  Premiere UXP companion panel
models/               model contract, exported manifests, local weights (ignored)
tests/                worker, cache, media, ONNX and HTTP integration tests
tools/                model export and end-to-end demo
scripts/              Windows/macOS build, worker launch and native installation
docs/                 workflow, design, validation and release gates
```

## Why the integration has two pieces

UXP manages Premiere workflows; the native effect owns Effect Controls and frame processing. Adobe's GPU effect examples use the shared effect SDK alongside the Premiere C++ SDK. This project rejects the After Effects host and does not use AEGP or AE compositions. [Adobe Premiere developer overview](https://developer.adobe.com/premiere-pro/), [Adobe GPU effect setup](https://ppro-plugins.docsforadobe.dev/gpu-effects-transitions/getting-started/).

A clip effect does not choose the exported sequence dimensions. Full-resolution Full SBS requires a double-width canvas/export. The standalone renderer directly produces 2W×H; the host adapter fits each complete eye independently into the supplied output canvas using Fit or Fill. Host verification is still required.

No proprietary Adobe headers are included. No checkpoint is redistributed as part of the source repository. Dependencies and models retain their own licenses; see [third-party notes](THIRD_PARTY.md).
