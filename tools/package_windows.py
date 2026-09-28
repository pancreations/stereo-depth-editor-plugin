"""Package an explicit Windows prerelease payload; never copy local runtime state."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.3.1"


def collect_payload(root=ROOT, build=None):
    build = build or root / "build-release"
    payload = {}
    for name, folder in (("StereoDepth.prm", "premiere"), ("StereoDepthPreview.prm", "preview")):
        payload[f"plugins/{name}"] = (build / "native" / folder / "Release" / name).read_bytes()
    cache = (build / "CMakeCache.txt").read_text()
    if "S3D_DISTRIBUTABLE:BOOL=ON" not in cache:
        raise ValueError("Build with -DS3D_DISTRIBUTABLE=ON; development binaries cannot be released")
    explicit = ["pyproject.toml", "THIRD_PARTY.md", "models/README.md", "native/gpu/runtime_kernels.cu",
                "scripts/start-worker.ps1", "tools/setup_worker.py", "tools/export_depth_anything.py",
                "tools/register_onnx_model.py", "tools/register_video_model.py"]
    for name in explicit:
        payload["runtime/" + name] = (root / name).read_bytes()
    for folder, pattern in (("worker/stereodepth_worker", "*.py"), ("constraints", "*.txt"), ("docs", "*.md")):
        for path in sorted((root / folder).rglob(pattern)):
            payload["runtime/" + path.relative_to(root).as_posix()] = path.read_bytes()
    if (root / "LICENSE").is_file():
        payload["runtime/LICENSE"] = (root / "LICENSE").read_bytes()
    panel = io.BytesIO()
    with zipfile.ZipFile(panel, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in ("manifest.json", "index.html", "style.css", "panel.js", "premiere.js"):
            archive.write(root / "uxp" / name, name)
    payload["StereoDepth-premierepro.ccx"] = panel.getvalue()
    template = (root / "scripts/release-native.ps1.in").read_text()
    template = template.replace("@EFFECT_HASH@", hashlib.sha256(payload["plugins/StereoDepth.prm"]).hexdigest())
    template = template.replace("@PREVIEW_HASH@", hashlib.sha256(payload["plugins/StereoDepthPreview.prm"]).hexdigest())
    for operation in ("Install", "Uninstall"):
        payload[f"{operation}.ps1"] = template.replace("@OPERATION@", operation).encode("utf-8-sig")
    payload["README-FIRST.txt"] = (root / "docs/WINDOWS_ZIP.md").read_bytes()
    payload["SHA256SUMS.txt"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(payload.items())
    ).encode()
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-release")
    args = parser.parse_args()
    payload = collect_payload(build=args.build)
    destination = ROOT / f"artifacts/releases/Stereo-Depth-Editor-Plugin-{VERSION}-windows-x64-preview.zip"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            archive.writestr("StereoDepth/" + name, data)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    print(json.dumps({"archive": str(destination), "files": len(payload), "sha256": digest}))


if __name__ == "__main__":
    main()
