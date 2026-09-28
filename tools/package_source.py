"""Build a source preview ZIP without SDKs, checkpoints, caches or local tokens."""
import hashlib
from pathlib import Path
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    destination = root / "artifacts/releases/StereoDepth-source-preview.zip"
    destination.parent.mkdir(parents=True, exist_ok=True)
    files = [root / name for name in ("README.md", "THIRD_PARTY.md", "CMakeLists.txt",
              "pyproject.toml", ".gitignore", ".gitattributes", "models/README.md")]
    license_file = root / "LICENSE"
    if license_file.is_file():
        files.append(license_file)
    extensions = {".py", ".cpp", ".hpp", ".cu", ".mm", ".r", ".cmake", ".in", ".txt",
                  ".md", ".json", ".html", ".js", ".css", ".ps1", ".sh", ".yml", ".yaml"}
    for folder in ("native", "worker", "uxp", "scripts", "tests", "docs", "tools", "constraints", ".github"):
        files.extend(p for p in (root / folder).rglob("*") if p.is_file() and p.suffix in extensions
                     and not any(part.startswith(".") or part.endswith(".egg-info") or part == "__pycache__"
                                 for part in p.relative_to(root / folder).parts))
    # Machine inventory is historical local data, not part of the source release.
    files = [p for p in files if p.name != "development-environment.txt"]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(set(files)):
            archive.write(path, "StereoDepth/" + path.relative_to(root).as_posix())
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    print(f"Created {destination} ({destination.stat().st_size:,} bytes)\nSHA-256 {digest}")


if __name__ == "__main__":
    main()
