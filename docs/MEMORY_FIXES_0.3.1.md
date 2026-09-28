# Memory and allocation fixes — 0.3.1

The reported Premiere crash has not been attributed to a faulting module. No matching Premiere exception stack or memory-exhaustion event was found in the inspected recent Windows/Adobe logs. A historical Windows RADAR pre-leak report for pythonw.exe predates the preview; it does not prove a current leak or cause of this crash. A live process sample is not a leak measurement.

Confirmed implementation defects and pressure points addressed:

- CUDA resize previously freed old buffers without invalidating the saved dimensions. If a subsequent allocation failed halfway through, rendering the previous resolution could incorrectly reuse incomplete buffers. `BufferPool` now invalidates immediately, owns each allocation as it succeeds, and frees every partial allocation on failure. Tests inject failure at each allocation and then retry the original resolution.
- Failed CUDA/NVRTC construction now releases acquired DLLs, compiler programs, context references and module resources. Previously a throwing constructor skipped the destructor.
- Depth Map / Parallax previews allocate only their required depth/output buffers: 24 bytes per source pixel versus the previous 146. Half-SBS stereo reserves 130 rather than 146 bytes per source pixel; Full SBS still needs 146. These figures exclude inference, Premiere, driver allocations and CPU frames.
- Native effect rendering applies cancellable backpressure before copying full-resolution float frames. Concurrent Adobe render requests no longer each retain a full copied image while waiting for the serialized inference/GPU stage. The CUDA path was already serialized.
- SBS Preview reuses a maximum of three owned image buffers. Busy frames are dropped rather than allocating another slot. Frame snapshots keep capture/upload pixels alive across re-entrant window calls. Display allocation/device failures retry at most once per second rather than continuously invalidating/repainting.
- External depth video decoding now retains at most two decoder generators, explicitly closes evicted/backward-seek generators, and bounds media metadata and normalization lookup tables. The existing depth-plane cache remains bounded at 128 MiB.

Validation:

- Four native CTest suites pass, including allocation failure/recovery and actual DLL/window callback tests.
- CUDA preview parity and inversion pass on the RTX 5070 Ti. Warm 1080p stereo in the standalone CUDA test remained about 10.13 ms/frame (excludes Premiere, inference and IPC).
- 29 Python regression tests pass, including decoder eviction/close checks and native HTTP cancellation tests.
- A 1,400-frame 3840×2160 preview stress run with repeated window resize/hide/reopen produced private-byte samples of **247,238,656 → 246,374,400 → 246,374,400** after warm-up and two further 600-frame batches. Every delivered mock Adobe frame is disposed. This demonstrates a memory plateau in the tested harness; it is not proof of Premiere crash resolution.
- Tests now identify preview windows by their owning process so an already-open Premiere preview cannot be mistaken for a leaked test window.

## Export / Render In to Out stalls

Two concrete stall paths were corrected before installation:

- High repair previously switched the entire stereo stage to the CPU reference. Background seeding and exemplar search now run on CUDA. The tested 1080p case went from 203.69 ms CPU to 18.74 ms GPU per frame; the small reference parity case had zero maximum color error. This excludes AI, IPC and Premiere and is not an export FPS guarantee.
- The synchronous bridge could treat a two-minute response timeout as a worker-start failure, spawn another worker, and retry up to 60 times. The new asynchronous WinHTTP bridge retries only a connection-refused startup, never resends an already-issued render after timeout, checks Premiere cancellation every 25 ms, validates complete response bodies, and waits for handle-close completion before releasing callback/buffer memory. There is one 120-second total request deadline, not 120 seconds repeatedly per operation. Cancelled inference may finish caching its current frame in the worker; the native render returns cancellation without returning a stale frame.

Validation includes 20 cancellations while response headers/body are delayed, timeout classification, truncated-response rejection and successful subsequent requests. Through the actual native DLL, 80 uniquely marked external-depth test frames were requested in mixed seek order with no repeats/reordering. Fast median/p95/max was 34.12/53.10/58.57 ms; High was 35.43/55.81/57.04 ms at 1624×1080 with zero displacement for the frame-identity check. These are SDK harness tests, not actual Premiere exports. Cached automatic AI through the rebuilt DLL also rendered SBS in 33.54 ms and depth in 29.83 ms after initialization.

Restart Premiere after installing both rebuilt native modules. The worker must also restart to activate decoder bounds; existing depth cache files remain reusable. Real-project reproduction, long playback/capture sessions, model changes and GPU memory exhaustion still require in-host verification. Uncached model inference remains expensive. Do not label the reported crash/export stall fixed solely from these tests.

## Installation verification

Both native modules and the companion UXP panel were installed after Premiere and Media Encoder closed on September 27, 2026. Installed native hashes match the tested build (see `artifacts/native-install.json` and `artifacts/preview-install.json`). The idle worker was restarted and reports version 0.3.1, using the existing AppData depth cache. The installed effect passed the 80-frame external-depth seek-order harness again: Fast median/p95/max 35.18/56.00/59.87 ms; High 34.95/54.56/59.40 ms. Actual Premiere project export verification remains outstanding.
