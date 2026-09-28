# Model selection for this Premiere effect

Reviewed 2026-09-27 after the user suggested Depth Anything 3. Initial DA-V2/VDA selection was a practical baseline, not the outcome of a DA3 quality comparison. DA3 should be evaluated before choosing a production default.

| Model option | Useful role | Tradeoff / verification needed |
|---|---|---|
| DA-V2 Small/Base/Large ONNX | Existing user weights; Large is the UI default | Frame-independent inference needs temporal treatment; establish quality on faces/hair |
| Video Depth Anything Small/Base/Large | Video-context option; streaming feature history; relative and metric choices | More state/VRAM; partial caches must replay context; Small relative tested so far |
| DA3Mono-Large | Dedicated high-quality monocular candidate | Bigger than Small baselines; per-frame inference alone is no guarantee against flicker |
| DA3 Small/Base any-view | Any-view architectures with Apache-2.0 weights | Current adapter uses one view; multi-view alignment requires a separate provider |
| DA3 Large/Giant/Nested | Heavier geometry/reconstruction candidates | Current weight licenses are CC-BY-NC-4.0; evaluate resource cost and rights before commercial use |

DA3 reports improved monocular depth performance and strong multi-view geometry. That does not establish superiority for moving faces, hair or stereo comfort; the published project describes detail/generalization as on par with DA2. Test both raw depth and the resulting stereo pair on matching footage. [Official DA3 project](https://depth-anything-3.github.io/).

DA3 is a family, not one checkpoint: Small/Base, Mono-Large and Metric-Large have Apache-2.0 licenses; the any-view Large/Giant/Nested entries are noncommercial. Mono-Large is a 0.35B-parameter dedicated relative monocular model. These license distinctions make it especially relevant for a distributable effect. [Official model table](https://github.com/ByteDance-Seed/Depth-Anything-3#%EF%B8%8F-model-cards), [Mono-Large model card](https://huggingface.co/depth-anything/DA3MONO-LARGE).

DA3-Streaming supplies long-video chunking and alignment. Its reported GPU memory depends strongly on resolution and window size: the published table includes 11.5–15.9 GB on a 504×154 setup and 18.7–28.3 GB on 504×378. Its reported A100 throughput is not an RTX Premiere benchmark. Use the detailed results instead of treating an under-12-GB headline as a universal requirement. [Official streaming evaluation](https://github.com/ByteDance-Seed/Depth-Anything-3/blob/main/da3_streaming/README.md).

## Integration implications

DA3 predicts distance, whereas the renderer consumes inverse depth/nearness. The DA3 adapter converts positive finite distance to inverse depth before caching, preserves crop/resize geometry and uses the worker's fixed clip-level normalization. Simply swapping distance into the nearness contract would reverse or distort stereo.

The DA3 path uses upstream PyTorch model modules and configs in the local worker. ONNX/TensorRT optimization can follow a verified geometry/shape contract. Multi-view mode additionally needs a window schedule, overlap reconciliation, deterministic resumption and testing on moving objects and cuts. Camera/point-cloud exports are not used. Giant configs still construct their upstream reconstruction heads for strict checkpoint compatibility; inference disables Gaussian reconstruction. Removing unused heads is a future memory optimization.

Because inference is cached, a slower high-quality model mainly changes preparation time and memory, not the cost of later stereo-strength/convergence edits. All sizes remain selectable, with DA-V2 Large as the default. DA3 Mono Large has passed an actual four-frame CPU inference + temporal + cache + native SBS test. No matched-footage quality comparison establishes a winner yet. Other DA3 weights remain downloadable and marked as awaiting checkpoint-specific validation. See [validation results](VALIDATION.md) for execution coverage.

## Existing user models

Copied from an existing local model collection into this project's `models` directory (not included in releases):

- `depth-anything-v2-small-fp16.onnx`
- `depth-anything-v2-base-fp16.onnx`
- `depth-anything-v2-large-fp16.onnx`

These have float32 NCHW inputs/float32 depth outputs with internal FP16 weights and dynamic spatial dimensions. Their ImageNet normalization matches the source project's inference helper. SHA-256 manifests are generated next to each copy; the original files are unchanged. This is copied existing model data, not a claim of independently verified export provenance.

Small initially failed an ONNX Runtime 1.23.2 CPU layer-normalization graph fusion. Explicit `graph_optimization: basic` fixes that initialization failure without modifying weights; the setting enters cache identity. A real Small inference + temporal + C++ stereo demo then passed. GPU graph optimization/performance still needs validation. The licenses recorded in manifests reflect the upstream DA-V2 weight family; verify any additional export provenance before redistribution.
