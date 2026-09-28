"""Reject private state, credential patterns and developer paths in release ZIPs.

This is a packaging hygiene check, not a vulnerability or dependency audit.
"""
import argparse
import io
from pathlib import Path, PurePosixPath
import re
import zipfile

PATTERNS = {
    "GitHub credential": re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})"),
    "AWS access key": re.compile(rb"(?:AKIA|ASIA)[A-Z0-9]{16}"),
    "private key": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "API secret": re.compile(rb"sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{32,}"),
    "developer home path": re.compile(rb"[A-Z]:[\\/]Users[\\/](?:pancr|pancreations)(?:[\\/]|\b)", re.I),
    "developer checkout": re.compile(rb"N:[\\/]3D_Plugin", re.I),
    "private model location": re.compile(rb"NV3D_VISION_" + rb"RESTORATION", re.I),
}
FORBIDDEN = {"connection.json", "hosts.yml", "development-environment.txt", ".env", ".git",
             ".venv", ".venv310", "__pycache__", "third_party", "AfterEffectsSDK_26.5_win",
             "Premiere Pro 26.0 C++ SDK"}


def inspect_archive(data, label):
    count = 0
    failures = []
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for info in archive.infolist():
            path = PurePosixPath(info.filename)
            if path.is_absolute() or ".." in path.parts or any(part in FORBIDDEN for part in path.parts):
                failures.append(f"{label}:{path}: forbidden path")
            if path.suffix.lower() in {".pdb", ".onnx", ".pth", ".safetensors", ".pyc", ".log"}:
                failures.append(f"{label}:{path}: unexpected binary/state")
            contents = archive.read(info)
            if path.suffix.lower() in {".ccx", ".zip"}:
                inner_count, inner_failures = inspect_archive(contents, f"{label}:{path}")
                count += inner_count
                failures.extend(inner_failures)
            # Scan both narrow and UTF-16LE strings without printing matched secrets.
            for name, pattern in PATTERNS.items():
                if pattern.search(contents) or pattern.search(contents.replace(b"\0", b"")):
                    failures.append(f"{label}:{path}: {name}")
            count += 1
    return count, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", type=Path, nargs="+")
    args = parser.parse_args()
    failures = []
    for archive in args.archives:
        count, found = inspect_archive(archive.read_bytes(), archive.name)
        failures.extend(found)
        print(f"{archive.name}: inspected {count} entries, {len(found)} findings")
    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
