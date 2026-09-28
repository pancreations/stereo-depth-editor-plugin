# Third-party dependencies and provenance

No Adobe SDK code is vendored or redistributed. Download those SDKs from Adobe and comply with their terms when building/distributing the native effect.

| Component | Role | Upstream |
|---|---|---|
| ONNX Runtime | Local CPU/CUDA/CoreML inference | https://github.com/microsoft/onnxruntime |
| NumPy | Worker float arrays | https://numpy.org/ |
| OpenCV | Resize and optical flow | https://opencv.org/ |
| Pillow | Grayscale/RGB image sequences | https://python-pillow.org/ |
| PyAV / FFmpeg | Source and depth video decoding | https://pyav.org/ |
| OpenImageIO (optional) | Float EXR depth | https://openimageio.org/ |
| PyTorch / torchvision | Checkpoint export and optional video-native inference | https://pytorch.org/ |
| Depth Anything V2 Small | Initial real model | https://github.com/DepthAnything/Depth-Anything-V2 |
| Video Depth Anything Small | Optional streaming video model | https://github.com/DepthAnything/Video-Depth-Anything |
| Depth Anything 3 | Optional single-view depth; complete model-family catalogue | https://github.com/ByteDance-Seed/Depth-Anything-3 |
| libcurl (macOS system library) | Native local worker HTTP transport | https://curl.se/libcurl/ |
| Apple AppKit/CoreML/Metal | macOS picker/launch and Apple inference backends | https://developer.apple.com/ |
| NVIDIA CUDA (optional) | GPU processing/runtime | https://developer.nvidia.com/cuda-toolkit |

Development DA-V2 checkout: `a561b849ebae10a6f5ef49e26c83cbbcd36c71bf`.
Development Video Depth Anything checkout: `4f5ae23172ba60fd7bc11ef671cca678842c7072`.
Development Depth Anything 3 checkout: `3d835ec1a5802d64a8b8b15f817a1ab54809bfe4`. Downloaded `depth-anything/DA3MONO-LARGE/model.safetensors`; checksum recorded in `models/da3mono-large.json`. Additional catalogue checkpoints retain the licenses shown in the model picker and upstream model cards.
Official Small checkpoint location: `depth-anything/Depth-Anything-V2-Small` on Hugging Face, filename `depth_anything_v2_vits.pth`. Local copies and exported ONNX weights are ignored by Git. Keep the upstream LICENSE/NOTICE alongside any redistributed code/weights; dependency distributions carry their own notices. A final redistributable installer needs a dependency/license inventory, particularly the exact FFmpeg build and NVIDIA runtime packaging terms.

The project contains original integration/processing code created for this workspace. No project-wide redistribution license has been selected by the owner yet.
