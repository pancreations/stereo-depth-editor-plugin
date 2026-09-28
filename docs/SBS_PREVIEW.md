# SBS Preview — part of the StereoDepth add-on

SBS Preview is a separate Windows capture window supplied by the add-on. Its companion native module, `StereoDepthPreview.prm`, receives Premiere's rendered monitor output through Mercury Transmit. The existing `StereoDepth.prm` effect creates the SBS image. No footage is moved out of the project or exported to temporary video files.

## Install and open

1. Build with `scripts/build.ps1`. Both Adobe SDKs are detected in this workspace.
2. Install the add-on with `scripts/install-native.ps1` from administrator PowerShell while Premiere and Media Encoder are closed. For an initial preview-only installation, `scripts/install-preview.ps1` can stage the new module while Premiere is open; a restart is required. Updating an existing preview module requires the hosts to close.
3. Install `artifacts/StereoDepth-premierepro.ccx` for the updated companion panel, then restart Premiere.
4. In **Edit > Preferences > Playback**, enable **Mercury Transmit** and select the video device **StereoDepth SBS Preview**. Its **Setup** button opens the window. Audio stays with Premiere's existing audio device.
5. Focus the timeline/Program Monitor and scrub or play. Set the clip effect's Preview to **Final Stereo (SBS)** for SBS capture. Selecting Depth Map displays that diagnostic output instead.
6. The add-on panel contains **Open SBS Preview**. Connect the local worker using `runtime/connection.json` first. This button reopens the native window; initial device enablement uses Premiere's Playback preferences. If the worker predates the preview update, restart it with `scripts/start-worker.ps1` without `-Setup` after closing Premiere.

If playback stops when switching to a capture/viewer app, turn off **Disable video output when in the background**, if present in your Premiere Playback preferences. Transmit follows the active monitor; clicking a Source Monitor can send that source instead of the timeline. Return focus to Program Monitor/timeline to restore sequence output.

## Capture and window controls

- Select the window titled **StereoDepth SBS Preview** in the external capture application. The title stays constant during playback.
- It is an unowned top-level Win32 window with its own HWND and `WS_EX_APPWINDOW`, discoverable through `EnumWindows`. It runs inside Premiere's process when hosted by Premiere; for tools that group targets by executable, expand/select **Adobe Premiere Pro.exe**, then **StereoDepth SBS Preview**. It is not a child of the UXP panel or Program Monitor.
- Drag an edge/corner to resize; the client image aspect stays locked to the transmitted frame's dimensions and pixel aspect ratio.
- Right-click for **Borderless capture window**, **Always on top**, **Fit to monitor**, **50% size**, **100% size**, and **Hide preview**.
- Keyboard shortcuts while this window has focus: **B** toggles borderless, **T** requests always-on-top, **F** fits to the current monitor, **Esc** restores borders. Drag inside the borderless window to move it; its edges still resize.
- Closing hides the preview, keeping the capture-window identity available. Reopen through the add-on panel or the device Setup button. Shutting down the transmitter closes its thread/window and releases graphics resources.
- For clean capture use the client area or borderless mode. There are no transport overlays over received frames. The image is fitted without cropping or stretching when Windows snapping imposes a different window shape.

1920×1080 Half SBS keeps a 16:9 packed aspect. 3840×1080 Full SBS keeps a 32:9 packed aspect. The viewer must interpret the matching SBS format; the window does not separately expand each eye. Full SBS still needs the double-width canvas described in [WORKFLOW.md](WORKFLOW.md).

## Rendering and limits

Premiere negotiates 8-bit full-range sRGB/BT.709-primary BGRA for this SDR viewing window. This conversion is confined to monitoring and does not modify the effect's float color pipeline or exports. This is not an HDR reference display or a lossless mastering output. HDR tone mapping and monitor color management need host validation.

The plugin copies each delivered PPix into an owned top-down buffer, immediately disposes every Adobe PPix, and hands the latest frame to a dedicated UI thread. A one-frame pending mailbox bounds backlog during scrubbing; if display is slower than playback, pending frames may be replaced rather than accumulated. Direct2D uploads the image and performs display scaling. It is not a zero-copy GPU-sharing/Spout output. Windows graphics capture and the standard `WM_PRINT`/`WM_PRINTCLIENT` GDI capture path are supported; specific third-party capturers still require testing.

Playback speed remains limited by Premiere rendering and depth generation. The preview does not run a second AI/stereo pipeline and receives frames served from Premiere's rendered preview cache as well as freshly rendered frames. It is video-only, leaves the playback clock/audio with Premiere, and does not promise frame-exact external audio synchronization.

## Validation

`s3d_preview_tests` loads the actual native module through `xTransmitEntry` and checks ABI negotiation, inactive/active/error PPix disposal, signed row pitches, lower-left to top-left conversion, pixel aspect, resize geometry, Half/Full-SBS aspect changes, capture pixels, borderless/hide/reopen controls, and shutdown. The interactive capture test must run on a desktop permitting window capture; sandboxed `PrintWindow` can return unrelated/blank pixels.

`tools/capture_preview.py` exercises the module and captures only the synthetic test window to `artifacts/sbs-preview-window.png`. The inspected 960×270 capture has the expected red/green upper halves and magenta/cyan lower halves without Premiere chrome.

**Known verification gap:** the test desktop did not retain `WS_EX_TOPMOST` despite accepting the window commands. The menu implements Windows' topmost request, but always-on-top behavior remains unverified. Real Premiere device discovery, playback/cached playback, switching monitors, external capture software and audio latency still need an in-host check. The mock SDK harness is not a substitute for that check.

## Files and SDK references

- `native/preview/transmitter.cpp`: Premiere module, format negotiation and PPix ownership.
- `native/preview/preview_window.*`: UI thread, Direct2D display and window/capture controls.
- `native/preview/geometry.cpp`: frame orientation and aspect math.
- `worker/stereodepth_worker/preview.py`: authenticated panel command to reopen the fixed native window; no executable/path supplied by clients.
- `uxp/index.html`, `uxp/panel.js`: add-on panel entry point.

[Adobe transmitter lifecycle and format negotiation](https://ppro-plugins.docsforadobe.dev/transmitters/transmitter-basics/). [Adobe pixel layout and orientation](https://ppro-plugins.docsforadobe.dev/universals/pixel-formats-and-color-spaces/).
