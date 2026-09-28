# Stereo Depth Editor Plugin releases

The Windows 0.3.1 prerelease provides a ZIP with the two native plug-ins, worker
source, companion CCX panel and optional PowerShell install/uninstall scripts.
See [Windows ZIP setup and safe removal](WINDOWS_ZIP.md). It is unsigned and needs
separate Python/dependency/model setup. It is not a production acceptance release.

## Build the Windows ZIP

Configure with both licensed Adobe SDKs and `-DS3D_BUILD_PREMIERE=ON
-DS3D_DISTRIBUTABLE=ON`, using `build-release` as the build directory. Then:

```powershell
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
python tools/package_windows.py
python tools/package_source.py
python tools/audit_release.py artifacts/releases/Stereo-Depth-Editor-Plugin-0.3.1-windows-x64-preview.zip artifacts/releases/StereoDepth-source-preview.zip
```

The Windows payload is selected explicitly; it excludes virtual environments,
SDKs, model weights/manifests, third-party checkouts, caches, connection tokens,
debug symbols and developer inventory. The build must enable `S3D_DISTRIBUTABLE`
so it cannot fall back to a private build-machine path. The runtime is located
through an adjacent `StereoDepth.runtime.txt` or an explicit `S3D_HOME` override.

The source archive uses an explicit source selection and excludes the same private
state. SHA-256 sidecars cover each archive; the Windows ZIP also lists payload
hashes. The audit checks credential patterns and private paths, including the CCX
contents and binary strings. It does not establish that software has no vulnerabilities.

## Validation

Run `tests/test_release_install.py` on Windows. It exercises preservation of
unrelated files and directories, modified files, backups, corrupt packages,
destination checks, junctions, forged receipts, repeated operations and dry runs.
Set `S3D_RUNTIME_LOCATION_TEST` to the built `s3d_runtime_location_tests.exe` for
relocation tests in `tests/test_runtime_location.py`.

Native core, buffer ownership and adapter tests pass locally. The desktop preview
capture test has a known pixel/orientation failure in this desktop environment.
Premiere playback, native dialogs, clean-machine AI setup and exports still need
real-host acceptance. See [release gates](RELEASE_CHECKLIST.md). macOS binaries
are not included.

CI runs worker/core tests on Windows, macOS and Linux. It does not validate Adobe
SDK integration or physical GPU/host behavior. Inspect the actual CI run before
promoting a release.

No project-wide open-source license has been selected. Publishing this preview
does not assign one. Third-party dependencies and downloaded models retain their
own terms; see [third-party notes](../THIRD_PARTY.md).
