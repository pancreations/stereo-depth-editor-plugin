# Local depth models

A fresh source release contains no weights, generated manifests, or upstream source checkouts. Use the [hardware setup](../docs/HARDWARE_COMPATIBILITY.md) first; descriptions of locally present models below are historical development records. On Apple silicon, begin with DA-V2 Small FP32 and explicitly select it after export.

The model picker defaults to **Depth Anything V2 Large**. All model sizes remain selectable; quality presets change inference resolution without changing the chosen model. The worker can stabilize adjacent frames using optical flow.

The user's existing Small/Base/Large FP16 ONNX exports have also been copied here and registered with JSON manifests. See [DA3 tradeoffs and compatibility notes](../docs/MODEL_TRADEOFFS.md). The exported official FP32 model and video-native model remain separate options.

The optional **Video Depth Anything** provider supports Small/Base/Large, relative and metric checkpoints, through the official streaming network in PyTorch. It retains temporal feature caches, resets at cuts, and replays preceding frames for partial cache rebuilds. Small relative has been exercised end to end; the other five checkpoints need individual validation. [Official Video Depth Anything repository](https://github.com/DepthAnything/Video-Depth-Anything).

## Choose or download a model

Connect the worker, then choose **AI depth model**. The 17 catalogue entries cover DA-V2 Small/Base/Large FP16 plus Small FP32; VDA Small/Base/Large in relative and metric variants; and DA3 Small, Base, Large 1.1, Giant 1.1, Mono Large, Metric Large and Nested Giant + Large 1.1. The panel shows installed state, license and validation status. **Download Selected Model** fetches official VDA/DA3 weights and registers a checksum manifest. Larger choices are not hidden or automatically downgraded. Custom manifests remain available through Browse.

The user's three DA-V2 FP16 models, the exported Small FP32 model, VDA Small relative, and DA3 Mono Large are present in this workspace. Other weights download on demand. The downloader needs the corresponding official source checkout at `third_party/Video-Depth-Anything` or `third_party/Depth-Anything-3`. It does not install Python packages or execute downloaded setup scripts.

```powershell
git clone https://github.com/DepthAnything/Video-Depth-Anything third_party/Video-Depth-Anything
git clone https://github.com/ByteDance-Seed/Depth-Anything-3 third_party/Depth-Anything-3
.\.venv310\Scripts\python.exe -m pip install -e ".[video,da3]"
```

Skip cloning a checkout that already exists. Install a matching CUDA PyTorch/torchvision pair using [the official PyTorch selector](https://pytorch.org/get-started/locally/) for RTX inference. The CUDA extra for ONNX Runtime is separate from CUDA PyTorch. Model weights are ignored by source control.

## Depth Anything 3

`DA3Depth` uses upstream PyTorch model modules and YAML configs directly, avoiding the upstream web/export application's dependency stack. It validates source/weight hashes, loads the selected architecture strictly, preserves aspect ratio, converts positive distance to inverse depth, and writes the same float cache used by the native renderer. DA3 Mono Large passed four-frame CPU and RTX 5070 Ti inference + temporal + native SBS demos; other DA3 variants are exposed but need checkpoint-specific and RTX validation.

Any-view DA3 variants currently run **one frame at a time**. The separate DA3-Streaming chunk/alignment pipeline is not integrated. Optional flow stabilization is available. Metric model output is used as nearness for stereo, not exposed as a calibrated distance measurement. Model choice changes cache identity; stereo remapping and Invert Depth Map do not.

The official projects distinguish model licenses: Small checkpoints are Apache-2.0; Base/Large models are CC-BY-NC-4.0. Do not bundle Base/Large into a commercial product without suitable rights. Model size and weight licensing are independent from the plugin's architecture. [Depth Anything V2](https://github.com/DepthAnything/Depth-Anything-V2), [Video Depth Anything licensing](https://github.com/DepthAnything/Video-Depth-Anything#license).

## Export

Obtain the official DA-V2 repository/checkpoint and create an export environment with PyTorch, torchvision and ONNX. A local development copy may exist under `third_party/Depth-Anything-V2`; it is ignored by source control. The tested checkout is recorded in THIRD_PARTY.md.

```powershell
.\.venv310\Scripts\python.exe tools/export_depth_anything.py `
  --repo third_party/Depth-Anything-V2 `
  --checkpoint models/depth_anything_v2_vits.pth `
  --encoder vits `
  --output models/depth_anything_v2_vits.onnx
```

The exporter writes both the ONNX graph and a JSON manifest containing its SHA-256, model contract, mean/std, provenance and license identifier. Select the JSON in the panel. The worker hashes the model before loading and rejects mismatch. It does not trust an arbitrary model to have the same normalization/output layout.

Input is float32 `1×3×H×W`, dynamic spatial dimensions, RGB normalized by ImageNet mean/std. Output is `1×H×W` or `1×1×H×W`, larger values meaning nearer. The worker aspect-preservingly resizes to a bounded long side and edge-pads to 14-pixel multiples. This differs from upstream's lower-bound short-side resize policy; it is included in the cache identity and must be visually validated at each preset. Fixed input-size or metric-depth ONNX graphs need a distinct provider/contract rather than silent reinterpretation.

## Quality presets

| Preset | Long-side inference bound | Intended use |
|---|---:|---|
| Preview | 280 | Lightweight proof of depth and framing |
| Balanced | 518 | Starting point for a shot |
| High | 756 | More spatial samples; higher cost |
| Maximum | 1036 | Highest implemented spatial sampling |

These presets currently control model resolution only. Temporal settings and native repair settings are explicit separate controls. Automatic final-export quality overrides and higher-quality AI inpainting presets are not implemented.

## Video Depth Anything

Install a compatible PyTorch/torchvision pair for your hardware plus `einops` and `easydict`. Use the official repository and `video_depth_anything_vits.pth` from the project's Small checkpoint link. Do not install the upstream demo's entire old requirements file over this worker environment.

```powershell
.\.venv310\Scripts\python.exe tools/register_video_model.py `
  --repo third_party/Video-Depth-Anything `
  --checkpoint models/video_depth_anything_vits.pth `
  --output models/video_depth_anything_vits.json
```

Select this manifest in the same panel model picker. Both checkpoint bytes and upstream Python sources are hashed; changed code requires deliberately regenerating the manifest. This provider imports code from a trusted local checkout. Its `repository` path is relative to the manifest, so move both together. For other checkpoints, use `--encoder vitb` or `--encoder vitl`, and `--metric` for metric weights; the catalogue downloader sets these automatically. Metric distance is inverted before stereo processing.

The streaming model receives aspect-preserved images padded to 14-pixel multiples and an analysis aspect ratio at most 1.75, then its output is cropped/resized back to the source. This bounds its internal window memory without stretching portrait/ultrawide content. Maximum with video context can consume substantially more VRAM than single-frame ONNX. A complete cache is reused without running the model; a partial cache is replayed from frame zero to rebuild context. No frame-independent resume shortcut is used.

Export validation should compare ONNX and PyTorch on landscape, portrait and square tensors at each supported shape. Pin the exporter/runtime version for a release. The tiny graph in the automated test verifies only runtime/preprocessing behavior; real model results are documented separately in `docs/VALIDATION.md`.
