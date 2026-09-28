"""Export an official Depth Anything V2 checkpoint to the worker ONNX contract.

Use a separate environment with torch, onnx, onnxscript and the upstream repository.
No checkpoints or upstream code are downloaded or executed automatically.
"""
import argparse
import hashlib
import json
import inspect
import math
from pathlib import Path
import sys
import textwrap
import types


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--encoder", choices=["vits", "vitb", "vitl"], default="vits")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo.resolve()))
    import torch
    from depth_anything_v2.dpt import DepthAnythingV2
    configurations = {
        "vits": {"features": 64, "out_channels": [48, 96, 192, 384]},
        "vitb": {"features": 128, "out_channels": [96, 192, 384, 768]},
        "vitl": {"features": 256, "out_channels": [256, 512, 1024, 1024]},
    }
    model = DepthAnythingV2(encoder=args.encoder, **configurations[args.encoder])
    model.load_state_dict(torch.load(args.checkpoint, map_location="cpu", weights_only=True))
    model.eval()
    # The upstream eager methods cast dynamic sizes to Python scalars and take a
    # square-only positional shortcut. A naive ONNX trace silently freezes 518x518.
    # Use size-based positional interpolation and remove the two head int casts.
    # We modify the loaded method in memory, not the user's upstream checkout.
    def dynamic_position(backbone, tokens, height, width):
        grid_size = math.isqrt(backbone.pos_embed.shape[1] - 1)
        dimension = backbone.pos_embed.shape[-1]
        grid = backbone.pos_embed[:, 1:].float().reshape(1, grid_size, grid_size, dimension).permute(0, 3, 1, 2)
        grid = torch.nn.functional.interpolate(grid, size=(height // backbone.patch_size, width // backbone.patch_size),
                                               mode="bicubic", align_corners=False)
        patches = grid.permute(0, 2, 3, 1).reshape(1, -1, dimension)
        return torch.cat((backbone.pos_embed[:, :1].float(), patches), dim=1).to(tokens.dtype)
    model.pretrained.interpolate_pos_encoding = types.MethodType(dynamic_position, model.pretrained)
    original_forward = type(model.depth_head).forward
    source = textwrap.dedent(inspect.getsource(original_forward))
    expected = "(int(patch_h * 14), int(patch_w * 14))"
    if expected not in source:
        raise RuntimeError("Upstream DPT head changed; review dynamic export adaptation before proceeding")
    source = source.replace(expected, "(patch_h * 14, patch_w * 14)")
    namespace = dict(original_forward.__globals__)
    exec(compile(source, "<dynamic-depth-head>", "exec"), namespace)
    model.depth_head.forward = types.MethodType(namespace["forward"], model.depth_head)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with torch.no_grad():
        torch.onnx.export(model, torch.zeros(1, 3, 518, 518), str(args.output), opset_version=17,
                          input_names=["image"], output_names=["depth"], dynamo=False,
                          dynamic_axes={"image": {2: "height", 3: "width"}, "depth": {1: "height", 2: "width"}})
    # Export is not complete until actual alternate shapes run with matching values.
    import numpy as np
    import onnxruntime as ort
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    runtime = ort.InferenceSession(str(args.output), sess_options=options, providers=["CPUExecutionProvider"])
    validation = []
    for height, width in [(154, 280), (280, 154), (518, 518)]:
        sample = torch.randn(1, 3, height, width, generator=torch.Generator().manual_seed(7))
        with torch.no_grad():
            expected_depth = model(sample).numpy()
        actual = runtime.run(["depth"], {"image": sample.numpy()})[0]
        if actual.shape != (1, height, width):
            raise RuntimeError(f"Export froze its output shape: {actual.shape}")
        np.testing.assert_allclose(actual, expected_depth, rtol=.002, atol=.002)
        validation.append({"height": height, "width": width, "max_abs_error": float(np.max(np.abs(actual - expected_depth)))})
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    manifest = {"id": "depth-anything-v2-" + args.encoder, "contract": "relative-inverse-depth-nchw-v1",
                "file": args.output.name, "sha256": digest, "output": "depth",
                "mean": [.485, .456, .406], "std": [.229, .224, .225],
                "license": "Apache-2.0" if args.encoder == "vits" else "CC-BY-NC-4.0",
                "upstream": "https://github.com/DepthAnything/Depth-Anything-V2",
                "export_variant": "dynamic-size-positional-interpolation-v1", "validation": validation}
    args.output.with_suffix(".json").write_text(json.dumps(manifest, indent=2))
    print(args.output.with_suffix(".json"))


if __name__ == "__main__":
    main()
