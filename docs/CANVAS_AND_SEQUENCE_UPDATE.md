# Canvas fitting and external sequence import

Built and checked on 2026-09-28.

- OUTPUT AND PREVIEW now includes Canvas Fit: Fit (show whole image) or Fill (crop edges). Both choices work in Half SBS and Full SBS for larger and smaller host output frames.
- Full SBS uses the complete input; it no longer removes the outer half under the assumption of a padded input. Remove the old prepared-canvas padding from projects that used that workaround.
- Browse Depth Source prompts whether a selected image should be imported as a numbered image sequence or held as a still. Cancel preserves the previous selection. The selected image starts the sequence; missing numbers are reported on import. PNG, TIFF, EXR and PFM are supported through the existing depth decoders.
- Import mode persists with the registered source. Sequence frames follow rational host timing and Depth Frame Offset; a numbered still is held at all times.

Validation: Release build succeeded. Core, GPU buffer ownership and Premiere adapter CTests passed. The actual plugin DLL rendered external depth through Half/Full SBS and both fitting options on larger/smaller odd-width canvases, with zero-strength output checked against complete source content. Eleven targeted worker/media/HTTP tests passed, including sequence versus still registration, persistence, nonzero starting numbers, seeking, offsets, gap rejection and source labels.

Installed into Adobe MediaCore with the previous effect backed up as `StereoDepth.prm.previous`; installed and built SHA-256 hashes match. The same DLL rendering checks passed against the installed effect. Restarted the idle local worker and verified sequence/still labels and actual requested depth frames through its live HTTP endpoints.

The separate SBS preview-window capture CTest failed with an all-white capture; no preview renderer code was changed in this update. Real Premiere dialog interaction, clip/sequence sizing, reduced-resolution previews and exports still require host validation. The effect fits the output frame Premiere supplies; it cannot change the sequence size or restore pixels removed upstream.

## Follow-up: padded input and RGB depth

The first canvas update still packed transparent host margins into both eyes. A regression with an opaque image placed at 75% of a larger input canvas reproduced the central gap against that build. The adapter now removes only fully transparent outer margins before requesting depth and rendering. The regression then passed for Half/Full SBS and Fit/Fill through the actual plugin DLL. Opaque black and partially transparent image content are preserved. Core, buffer-ownership and adapter CTests also passed.

RGB/RGBA, palette and color PFM depth images are sampled as grayscale intensity in memory on demand. Grayscale values and float range are preserved; alpha is ignored for depth. No converted images or depth-frame disk cache are written by direct image-sequence loading. Sixteen targeted media, render-service and HTTP tests passed, including seek-order color frames and an assertion that only the tiny source registration is persisted.

The follow-up effect was installed with matching hashes, and the idle worker restarted. The installed DLL successfully loaded a true RGB PNG depth sequence and passed both ordinary canvas-size checks and the padded-input gap regression in all four Half/Full and Fit/Fill combinations. Test artifacts are under `artifacts/rgb-depth-installed-test`. The user's exact Premiere frame still needs visual confirmation after reopening the host.
