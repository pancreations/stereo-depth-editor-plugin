"""Create a checksum manifest for a trusted official Video Depth Anything checkout."""
import argparse
import json
import os
from pathlib import Path

from stereodepth_worker.cache import digest_file, identity


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--encoder", choices=["vits", "vitb", "vitl"], default="vits")
    parser.add_argument("--metric", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve(strict=True); checkpoint = args.checkpoint.resolve(strict=True)
    files = sorted(list((repo / "video_depth_anything").rglob("*.py")) + list((repo / "utils").rglob("*.py")))
    if not files:
        raise ValueError("Not a Video Depth Anything checkout")
    output = args.output.resolve(); output.parent.mkdir(parents=True, exist_ok=True)
    manifest = {"id": "video-depth-anything-" + args.encoder + ("-metric" if args.metric else "-relative"), "contract": "video-depth-anything-stream-v1",
                "encoder": args.encoder, "metric": args.metric, "file": os.path.relpath(checkpoint, output.parent), "sha256": digest_file(checkpoint),
                "repository": os.path.relpath(repo, output.parent).replace(chr(92), "/"),
                "source_sha256": identity({p.relative_to(repo).as_posix(): digest_file(p) for p in files}),
                "license": "Apache-2.0" if args.encoder == "vits" else "CC-BY-NC-4.0", "upstream": "https://github.com/DepthAnything/Video-Depth-Anything"}
    output.write_text(json.dumps(manifest, indent=2))
    print(output)


if __name__ == "__main__":
    main()
