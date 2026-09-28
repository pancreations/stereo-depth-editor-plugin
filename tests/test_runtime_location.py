import os
from pathlib import Path
import shutil
import subprocess

import pytest


@pytest.fixture
def probe(tmp_path):
    source = os.environ.get("S3D_RUNTIME_LOCATION_TEST")
    if not source:
        pytest.skip("Set S3D_RUNTIME_LOCATION_TEST to the native Windows test executable")
    executable = tmp_path / "module folder [1]" / "probe.exe"
    executable.parent.mkdir()
    shutil.copyfile(source, executable)
    root = tmp_path / "runtime caf\u00e9 with spaces"
    (root / "worker/stereodepth_worker").mkdir(parents=True)
    (root / "pyproject.toml").write_text("# fixture")
    return executable, root


@pytest.mark.parametrize("case", ["valid", "crlf", "missing", "relative", "marker", "moved", "extra"])
def test_runtime_location_uses_module_and_validates_configuration(probe, case):
    executable, root = probe
    config = executable.parent / "StereoDepth.runtime.txt"
    text = f"StereoDepth runtime v1\n{root}\n"
    expected = str(root)
    if case == "crlf":
        text = text.replace("\n", "\r\n")
    elif case == "relative":
        text = "StereoDepth runtime v1\nrelative/path\n"
    elif case == "marker":
        text = f"unrelated\n{root}\n"
    elif case == "moved":
        text = f"StereoDepth runtime v1\n{root / 'missing'}\n"
    elif case == "extra":
        text += "extra line\n"
    if case != "missing":
        config.write_bytes(text.encode("utf-8"))
    if case not in ("valid", "crlf"):
        expected = "--missing" if case == "missing" else "--invalid"
    result = subprocess.run([str(executable), expected], cwd=root, capture_output=True)
    assert result.returncode == 0, result.stderr
