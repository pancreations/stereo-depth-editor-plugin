"""Run the actual generated installer/uninstaller against disposable directories."""
import hashlib
import os
from pathlib import Path
import subprocess

import pytest

pytestmark = pytest.mark.skipif(os.name != "nt", reason="Windows PowerShell installer")
ROOT = Path(__file__).resolve().parents[1]
NAMES = ("StereoDepth.prm", "StereoDepthPreview.prm", "StereoDepth.runtime.txt")


@pytest.fixture
def release(tmp_path):
    package = tmp_path / "package with spaces [1]"
    (package / "plugins").mkdir(parents=True)
    (package / "runtime/worker/stereodepth_worker").mkdir(parents=True)
    (package / "runtime/pyproject.toml").write_text("# fixture")
    template = (ROOT / "scripts/release-native.ps1.in").read_text()
    for name, key in zip(NAMES[:2], ("EFFECT", "PREVIEW")):
        data = ("fixture " + name).encode()
        (package / "plugins" / name).write_bytes(data)
        template = template.replace(f"@{key}_HASH@", hashlib.sha256(data).hexdigest())
    for operation in ("Install", "Uninstall"):
        (package / f"{operation}.ps1").write_text(template.replace("@OPERATION@", operation), encoding="utf-8-sig")
    target = tmp_path / "Adobe/Common/Plug-ins/7.0/MediaCore/StereoDepth"
    return package, target


def run(release, operation, *args, ok=True):
    package, target = release
    result = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                             str(package / f"{operation}.ps1"), "-PluginDirectory", str(target), *args],
                            capture_output=True, text=True)
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result


def test_roundtrip_preserves_other_files_and_all_directories(release):
    package, target = release
    target.mkdir(parents=True)
    sentinels = [target / "OtherPlugin.prm", target / "StereoDepth.prm.previous",
                 target / "notes.txt", target / "nested/user-project.prproj",
                 target.parent / "OtherVendor.prm", package / "runtime/models/user.bin"]
    for path in sentinels:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"must survive exactly")
    run(release, "Install")
    run(release, "Install")
    assert all((target / name).is_file() for name in NAMES)
    run(release, "Uninstall", "-WhatIf")
    assert all((target / name).is_file() for name in NAMES)
    run(release, "Uninstall")
    run(release, "Uninstall")
    assert target.is_dir()
    assert all(not (target / name).exists() for name in NAMES)
    assert all(path.read_bytes() == b"must survive exactly" for path in sentinels)


def test_modified_files_preserved(release):
    _, target = release
    run(release, "Install")
    for name in NAMES:
        (target / name).write_bytes(b"modified or newer file")
    run(release, "Uninstall")
    assert all((target / name).read_bytes() == b"modified or newer file" for name in NAMES)


def test_unknown_existing_file_stops_entire_install(release):
    _, target = release
    target.mkdir(parents=True)
    (target / NAMES[1]).write_bytes(b"someone else's file")
    run(release, "Install", ok=False)
    assert not (target / NAMES[0]).exists()
    assert (target / NAMES[1]).read_bytes() == b"someone else's file"


def test_one_newer_plugin_preserves_entire_installation(release):
    _, target = release
    run(release, "Install")
    (target / NAMES[0]).write_bytes(b"newer release")
    run(release, "Uninstall")
    assert all((target / name).is_file() for name in NAMES)


def test_corrupt_payload_stops_entire_install(release):
    package, target = release
    (package / "plugins" / NAMES[1]).write_bytes(b"corrupted")
    run(release, "Install", ok=False)
    assert not target.exists()


def test_directory_disguised_as_plugin_stops_uninstall(release):
    _, target = release
    run(release, "Install")
    (target / NAMES[1]).unlink()
    (target / NAMES[1]).mkdir()
    protected = target / NAMES[1] / "user-file"
    protected.write_bytes(b"keep")
    run(release, "Uninstall", ok=False)
    assert (target / NAMES[0]).is_file()
    assert protected.read_bytes() == b"keep"


def test_install_whatif_creates_nothing(release):
    _, target = release
    run(release, "Install", "-WhatIf")
    assert not target.exists()


def test_refuse_wrong_destination(release):
    package, target = release
    run((package, target.parent), "Install", ok=False)
    run((package, target.parent), "Uninstall", ok=False)
    assert not target.parent.exists()


def test_refuse_junction_before_removing_anything(release, tmp_path):
    _, target = release
    run(release, "Install")
    (target / NAMES[1]).unlink()
    outside = tmp_path / "unrelated"
    outside.mkdir()
    sentinel = outside / "user-file"
    sentinel.write_bytes(b"keep")
    # Native PowerShell only; remove the junction itself nonrecursively during cleanup.
    command = "New-Item -ItemType Junction -Path $args[0] -Target $args[1] | Out-Null"
    helper = tmp_path / "junction.ps1"
    helper.write_text(command)
    result = subprocess.run(["powershell.exe", "-NoProfile", "-File", str(helper), str(target / NAMES[1]), str(outside)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    try:
        run(release, "Uninstall", ok=False)
        assert (target / NAMES[0]).is_file()
        assert sentinel.read_bytes() == b"keep"
    finally:
        os.rmdir(target / NAMES[1])


def test_tampered_receipt_cannot_add_targets(release, tmp_path):
    _, target = release
    run(release, "Install")
    protected = tmp_path / "project.prproj"
    protected.write_bytes(b"keep")
    (target / "uninstall-manifest.json").write_text('{"files":["../../../../project.prproj"]}')
    run(release, "Uninstall")
    assert protected.read_bytes() == b"keep"
    assert (target / "uninstall-manifest.json").is_file()
