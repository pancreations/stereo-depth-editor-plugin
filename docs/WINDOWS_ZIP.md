# Stereo Depth Editor Plugin 0.3.1 - Windows x64 preview

This is an unsigned preview for Premiere Pro, with a copy-based installer and a
conservative uninstaller. Real Premiere playback/export acceptance is still pending.
Python, AI weights, third-party model repositories and GPU dependencies are not
bundled. A first-time setup needs internet access and several GB of disk space.

## Install

1. Extract the entire ZIP to a permanent, writable folder you own, such as
   `%LOCALAPPDATA%\StereoDepth-0.3.1`. Keep this folder in place after installation.
2. Close Premiere and Media Encoder. Open PowerShell as administrator, change to
   the extracted `StereoDepth` folder, and run:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1`
   This copies only the two native plug-ins and a runtime-location text file to
   `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\StereoDepth`.
   A differing existing file is preserved and installation stops; use that
   version's uninstaller first. No environment variables or registry entries change.
3. In a NORMAL, non-administrator PowerShell, change to the extracted `runtime`
   folder. With 64-bit Python 3.11 installed, run:
   `py -3.11 tools/setup_worker.py --backend cuda`
   Use `--backend cpu` for a CPU worker. For CPU native stereo also set
   `S3D_STEREO_DEVICE=cpu` in the environment inherited by Premiere.
4. Follow `runtime/models/README.md` for AI models. The default DA-V2 Large model
   is NOT bundled. External depth does not require AI weights, but still needs
   the worker dependencies. Optional DA3/VDA models need their upstream source
   checkouts as documented there.
5. Optionally open `StereoDepth-premierepro.ccx` with Adobe Creative Cloud to
   install the companion panel. Adobe manages that panel's installation.
6. Restart Premiere. Apply `StereoDepth > 2D to 3D`. Test a short clip first.

Do not move the extracted folder after setup: Python environments may contain
absolute paths. Set up a fresh extraction if relocating. An existing `S3D_HOME`
environment override takes precedence over the runtime-location file.

## Manual copy / drag and drop

You can copy `plugins/StereoDepth.prm` and `plugins/StereoDepthPreview.prm` into
the MediaCore `StereoDepth` folder above. Do not replace unknown existing files.
Also create `StereoDepth.runtime.txt` there as UTF-8 without a byte-order mark,
containing exactly these two lines (replace the example with your actual path):

    StereoDepth runtime v1
    C:\Users\YOUR-NAME\AppData\Local\StereoDepth-0.3.1\StereoDepth\runtime

The optional Install.ps1 performs this small copy/configuration step for you.
Copying the two plug-ins alone does not install the Python/AI dependencies.

## Remove safely

Close Premiere and Media Encoder. From the SAME extracted release folder, run
the following in an administrator PowerShell. Preview first:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\Uninstall.ps1 -WhatIf
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Uninstall.ps1

The uninstaller considers ONLY `StereoDepth.prm`, `StereoDepthPreview.prm`, and
`StereoDepth.runtime.txt` in the MediaCore StereoDepth folder. Each is removed
only when its SHA-256 matches this release's expected content. Changed or newer
files cause the entire removal to stop, preserving shared files another version
may still need. It rejects symbolic links/junctions in destination paths.
It never deletes a directory and never follows a file list from a receipt.
It leaves the extracted runtime, Python, downloaded models, caches, .previous
backups, other plug-ins, projects and media alone. This intentionally leaves files
behind when ownership is uncertain. Keep the extraction for future uninstall.

For a completely manual uninstall, delete just the two native plug-in files and
your `StereoDepth.runtime.txt` after verifying they belong to this add-on. Never
delete MediaCore, Adobe, or the whole StereoDepth folder to remove the effect.
Remove the optional UXP panel through Creative Cloud's plug-in manager.

## Release contents

`SHA256SUMS.txt` records every packaged payload file. The adjacent `.zip.sha256`
download verifies the archive. No developer virtual environment, model weights,
SDK, local connection token, cache, debug symbols or development inventory is
included. Source and third-party setup notes live in the public repository.
