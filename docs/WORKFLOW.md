# Editor workflow and parameter semantics

The following is the intended workflow for the SDK-built adapter. The standalone pipeline is tested; native UI/host timing, full-size canvas behavior and export still need Premiere validation.

## First working path: external depth

1. Use progressive, constant-frame-rate media with square pixels, no speed change, and a matching depth sequence/video at the same resolution and frame rate.
2. In native Effect Controls, open **DEPTH SOURCE** and click **Browse Depth Source...**. Select the first external depth image or a video. For images, the import prompt offers **Yes** (numbered image sequence), **No** (single still depth pass), or **Cancel** (keep the current selection). Successful import enables override and clears the optional prepared-cache slot.
3. Direct image sequences use the host frame rate and rational render time. Numbering can start at 1001: select that first image. The selected image is frame zero, and subsequent files with matching prefix, extension and number padding follow it. Still import holds the selected image at every frame, even if its filename is numbered. Missing numbered frames fail during import. The source label identifies sequence imports and their frame count. For an explicit sequence rate or EXR channel other than Z, use the panel to prepare an external cache.
4. Direct external loading needs no Generate Depth Cache step. Keep Prepared Cache Slot at zero. Optional panel-generated caches retain their immutable slot bindings and must match the override state.
5. Use **Depth Map** preview to verify the processed depth and timing. Set interpretation, normalization, near/far, then select Final Stereo and adjust strength/convergence.

The initial adapter maps `floor(hostTime / hostTimeScale × sourceFPS) + Depth Frame Offset`. There is no mutable playback counter. Host `current_time` behavior with trims, nested sequences, proxies and exports is a release gate. Verify a numbered depth test clip; adjust the offset only for constant offsets. Speed ramps, reversals, frame blending, optical-flow retiming and variable frame rate require rendering to a matching CFR intermediate first. Do not assume an offset can repair a changing time map.

Numeric external-source IDs and prepared-cache slots are serialized by this adapter; source paths remain in the local cache registry. Source identity and root/binding portability must be validated by the editor when relinking. A portable arbitrary-data parameter and Premiere File Registration Suite integration remain release work.

## Blender/CG depth

- PNG/TIFF: grayscale values are preserved. RGB/RGBA images are read directly as grayscale intensity (0.2126 R + 0.7152 G + 0.0722 B), ignoring alpha. This happens in memory for requested frames; direct image-sequence loading writes no converted images or depth-frame disk cache. Existing grayscale/float precision is retained. RGB PFM and palette PNG images are also accepted. Color is interpreted as brightness, not as an inverse of a false-color depth palette.
- EXR: install OpenImageIO and choose the actual channel (`Z`, `Depth.Z`, etc.). Float samples are retained in the cache. No gamma/color conversion is applied to EXR data. Multichannel files with a missing requested channel fail.
- Blender Mist usually provides a convenient finite range. Blender Z is camera distance; use Black = Near after fixed-range normalization, or disable normalization and set Near Point to the near distance and Far Point to the far distance with White = Near. The signed near/far denominator supports decreasing raw ranges. Do not invert twice.
- Infinite sky/background values must be replaced by a finite far clip in the render/compositor. Automatic infinity replacement is deliberately absent because it can reverse intended interpretation.
- Video depth is decoded through PyAV to gray16. Codec/range conversion can change values. Prefer lossless grayscale FFV1 or image sequences for precise depth; JPEG/H.264 depth may ring/band. The worker supports PyAV-readable media, not every proprietary format Premiere can import. Transcode unsupported sources to an image sequence or supported intermediate.
- External depth is still eligible for optional temporal processing; leave it off for an accurate CG pass unless needed.

## AI depth

Choose a DA-V2 or DA3 model in native Effect Controls, turn override off and leave Prepared Cache Slot at zero. Rendering requests infer and cache missing frames automatically. Use the panel to download missing models or prepare Video Depth Anything / temporally stabilized caches. DA-V2 Large is the default. All sizes and families remain visible; use Download Selected Model for an uninstalled VDA/DA3 checkpoint, or browse a custom manifest. See [the model guide](../models/README.md) for dependencies. CUDA is the default device; CPU can validate the workflow. Four presets cap the model's long input side at 280/518/756/1036 and pad to multiples of 14 while keeping aspect ratio. The preset never substitutes a smaller model.

All providers produce inverse depth (larger = nearer), converting distance outputs where needed. This is not a physically calibrated camera reconstruction. Hair, motion blur, reflections, glass and disoccluded content remain limitations. DA-V2 ONNX and the current DA3 adapter infer independent frames; Video Depth Anything retains streaming temporal context. Optional flow stabilization applies to all. The video provider caps/pads its analysis input and replays all preceding source frames when rebuilding an incomplete cache.

**Invert Depth Map** in Effect Controls flips the remapped depth range for both AI and external passes. Depth Map preview shows the processed, inverted map. It reuses the raw cache and does not rerun inference. Depth Interpretation separately declares White = Near or Black = Near; enabling Invert reverses that interpretation. Depth Direction and Eye Order remain separate stereo controls.

For reliable HDR analysis, use an explicitly prepared SDR proxy with identical geometry/timing; the offline worker rejects PQ/HLG source inference. Automatic native inference currently clamps its analysis copy to 0..1 without a color-managed HDR-to-SDR transform; it is not validated for HDR depth quality. The effect still reprojects Premiere's original float color frame. External override accepts HDR source media because it does not run inference. Its optional flow guide is an 8-bit analysis conversion, never the output color.

## Depth remap order

CPU reference: raw float depth → optional fixed clip-range normalization → white/black interpretation → `(value − Far)/(Near − Far)` → inversion → scale around 0.5 and offset → contrast around 0.5 → signed gamma power `sign(d)×abs(d)^(1/gamma)` → optional clamp → fidelity/blur bilateral filter → resample to render dimensions.

Neutral Contrast/Gamma/Scale = 1, Offset = 0, Near = 1, Far = 0. Fidelity 100 preserves raw fine detail; lower values increase filtering. Edge Preservation controls rejection across depth discontinuities. Fidelity cannot invent missing hair detail. Normalize uses a fixed range, not separate min/max per frame. Automatic/direct rendering anchors the range to the first requested frame for each source/settings combination; offline caches sample across the clip. The first 4096 frames supply bounded range samples for long clips; split long sources into shots when needed.

The CUDA path upsamples raw depth to render size before remapping/filtering. Nonlinear settings can therefore differ from the CPU reference at differing depth resolutions; expanded parity validation remains required.

## Stereo math

Pair disparity in pixels is:

```text
clamp((depth − convergence) × strength × eyeSeparationPercent × W / 100,
      −maximumParallaxPercent × W / 100,
      +maximumParallaxPercent × W / 100)
```

The left eye shifts +half of this and the right −half. W is the full per-eye source width before SBS packing. Positive near displacement creates crossed disparity. Convergence changes the zero-disparity plane independently of the depth remap; extreme values encounter the same parallax limit. Direction reversal changes the shift sign, not the physical visibility ordering used for occlusion. Eye Order swaps the packed eyes afterward.

## Half SBS, Full SBS and canvas fitting

Under **OUTPUT AND PREVIEW**, select **Output** and **Canvas Fit**:

- **Fit (show whole image)** is the default. It scales up or down while preserving the image proportions and centers it with black bars where needed.
- **Fill (crop edges)** scales up or down to cover each eye's area completely and crops the excess equally around the center.
- **Half SBS** compresses each eye horizontally by 2 for stereo playback. A 1920x1080 image fits two 960x1080 packed eyes in a 1920x1080 canvas.
- **Full SBS** preserves each eye's proportions without that compression. A 1920x1080 image fits two 1920x1080 eyes in a 3840x1080 canvas. Smaller or larger canvases scale the eyes accordingly.

Each eye is fitted independently to the host-provided output canvas, including odd widths (the second eye gets the extra column). Fully transparent outer margins supplied by Premiere are removed before inference/reprojection, so a smaller clip on a larger canvas does not acquire duplicated padding and a central SBS gap. Actual black image pixels and partially transparent pixels are retained. External depth must describe the picture without the host-added transparent canvas padding. Color and depth are reprojected together before fitting.

Use the complete source image. The adapter no longer extracts the center half of a specially padded Full-SBS input. Existing projects using that old workaround should remove the extra source padding. Original and diagnostic previews use the same Fit/Fill selection as a single image.

The effect cannot change sequence/export settings or recover pixels Premiere cropped before this effect. It fits the output frame supplied by Premiere; if a clip effect receives source-sized frames, apply it in a sequence-sized context to control the final canvas. Full-resolution Full SBS still needs a double-width output sequence/export. Validate nesting, Motion/Scale ordering and reduced-resolution previews in Premiere before export.

## Repair and previews

Off uses original-camera pixels at unfilled locations; Fast stretches nearest visible scanline samples; High seeds holes from farther/background edges, then searches nearby immutable visible 3×3 patches to synthesize matching texture. Radius bounds the search; softness blends with the conservative seed. These are heuristic repairs, not recovery of hidden scene content. In 0.3.1 High runs on CUDA instead of switching the effect to the CPU reference. It still costs more GPU time and needs visual validation on real footage. AI inpainting remains unimplemented; the native provider interface accepts a future model without pretending one is installed.

Depth Map displays the remapped/filter result. Left/Right isolate the eye renders. Stereo Difference shows absolute RGB difference. Parallax is green near zero, red/blue at opposite displacement limits. Occlusion Mask is red for left-eye holes and blue for right-eye holes, captured before repair. Original bypasses cache reads in the native adapter. Diagnostic single-eye views follow Canvas Fit on the current output canvas.

## Cache and temporal behavior

Stereo/depth remapping changes do not enter the inference identity. Model bytes, preprocessing, device/runtime, quality, source contents and sampling do. Temporal settings create a derived cache and reuse raw inferred frames. Flow is computed offline from the current and previous source frame, with forward/backward, color-change, depth-agreement and edge rejection; hard cuts reset. It uses the previous raw depth rather than recursively filtered history to reduce lag.

Clear Current Depth Cache removes the displayed derived/raw entry, not every cache on disk. If temporal processing produced a derived entry, its raw inference cache remains reusable. Missing caches fail rendering; regenerate before export. Do not clear or relocate a cache during rendering. Disk quota, automatic eviction and cross-machine relinking remain release work.
