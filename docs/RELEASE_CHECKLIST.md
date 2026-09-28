# Release gates — not yet a production plugin

Compatibility status: [platform guide](HARDWARE_COMPATIBILITY.md), [validation record](COMPATIBILITY_VALIDATION.md), and [GitHub source preview](GITHUB_RELEASE.md). macOS is experimental and requires physical Mac validation.

Do not ship the current project as a finished, real-time Premiere effect. The supplied Adobe SDKs now build the native binary, but in-host validation and several requested features remain outstanding.

| Area | Current implementation | Required before production |
|---|---|---|
| Native processing | Compiled CPU reference, tested float pipeline | Profile and budget allocations/latency at 4K; bounded scratch/LRU |
| Premiere effect | Compiled `.prm`, PiPL and SDK callback smoke tests, parameter mapping | In-Premiere load/render/export tests |
| UXP | Local CCX installed; models/video-cache tabs, authenticated jobs and selection/apply/bind | Load/debug in UDT; verify manifest/UI/API signatures and undo behavior |
| External depth | Native Browse button/registry; PNG/TIFF/PFM, optional EXR and PyAV video; DLL render test | Real Blender/Unreal EXR validation; codec/range tests; source relinking |
| Half/Full SBS | CPU packers tested | Premiere and AME double-width nesting/preview/export validation |
| AI | DA-V2 ONNX, DA3 single-view adapter, full catalogue/download controls | Validate every checkpoint, RTX benchmarks, provider packaging, shot quality review |
| Video-native AI | VDA Small/Base/Large relative/metric adapter; Small relative tested; complete-cache reuse and ordered replay | Remaining checkpoint validation, RTX performance, long-video VRAM and face/hair/shot-change validation |
| Temporal | Offline bidirectional flow with cut/rejection masks | Difficult motion/occlusion/shot-change evaluation; GPU implementation |
| Gap repair | Fast stretch, background edge extension and bounded exemplar content-aware fill; native High now on CUDA | Visual/temporal validation, better thin-object reconstruction, expanded GPU parity |
| AI inpainting | Injectable native provider contract and explicit unsupported error; not offered as working UI | Licensed model, temporal consistency, VRAM budget, visual/export tests |
| GPU render | Native NVRTC CUDA path; preview/inversion parity and warm 1080p benchmark | PrGPUFilter registration, device suites, stream lifetimes, parity and stress tests |
| Cache | Checksummed atomic raw/derived entries, bindings | Disk quota/eviction; source mutation checks; leases during export; portable serialization |
| Synchronization | Rational CFR indices + explicit offset | Host source-time mapping with trims, nests, proxies, speed, reverse and multicam |
| Color | Float color core; no tone mapping | Premiere HDR/alpha/PAR/transfer/primaries tests; gamut-safe inference proxy preparation |
| Delivery | Local build/launch/install scripts | Runtime bundling, signing, versioned parameter migration, install/uninstall QA |

## Host acceptance matrix

- Effect appears in Premiere Effects and editable parameters appear in Effect Controls; rejects After Effects loading.
- Numbered color and depth clips remain aligned at start/middle/end with nonzero source trims, 23.976/29.97 rates, nested clips, playback scaling and AME export.
- Random seeks, backward scrubbing, concurrent export/render requests and reopened projects produce identical source-frame selection.
- Half SBS 1920×1080 and Full SBS 3840×1080 deliver the expected horizontal geometry and correct eye order; repeat for portrait, 4K and odd-sized source images.
- Cache slot/override mismatch, missing cache, corrupt header/payload, missing model, incompatible GPU and cancellation give useful errors. Original preview remains usable without a cache.
- Stereo/convergence/remap adjustments never call inference. Changed model/source/inference settings invalidate the raw identity. Changed temporal settings reuse raw frames.
- Black backgrounds are source content, not unfilled reprojection holes. Transparent source alpha behaves as expected through the host's premultiplied representation.
- Float negative/over-one values and HDR tags survive an appropriate float/HDR export; validate PQ/HLG and a graded source. Check resampling in host color representation.
- Missing native binary and worker disconnect show actionable UI errors. Duplicate effect application, undo/redo, cache binding and panel destruction do not leave stale operations/timers.
- User cache relocation, project collect/consolidate, AME separate process and another workstation have explicit supported behavior.
- Real model tests include faces, hair, thin foreground objects, moving occluders, reflections, scene cuts, whip pans and fine texture. Use binocular review; a plausible single-eye image is insufficient.
- RTX tests record device, driver, ORT/CUDA/cuDNN version, model, input size, full-resolution frame size, FPS, VRAM peak and transfer trace. No undocumented CPU fallback.

## Suggested next development order

1. Acquire SDKs, compile adapter, validate manual depth + Half SBS in Premiere.
2. Validate Full-SBS prepared canvas and source-time mapping; add portable instance/source binding.
3. Load the UXP panel and verify real ONNX cached depth jobs against the native effect.
4. Register CUDA kernels through the current Premiere GPU sample; add GPU resident caches and parity tests.
5. Validate video-native inference on difficult footage; add GPU flow stabilization.
6. Improve content-aware fill; add AI inpainting, quality-specific reconstruction and shipping installers.

Passing unit tests does not close these host and visual acceptance gates.
