"""Register an existing DA-V2-compatible ONNX export without altering its weights."""
import argparse
import json
from pathlib import Path

import onnx

from stereodepth_worker.cache import digest_file


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("--weight-license", default="Unverified: confirm model/export provenance")
    parser.add_argument("--graph-optimization", choices=["all", "basic", "disabled"], default="all")
    args = parser.parse_args()
    model = args.model.resolve(strict=True)
    graph = onnx.load(str(model), load_external_data=False)
    inputs = [v for v in graph.graph.input if v.name not in {i.name for i in graph.graph.initializer}]
    if len(inputs) != 1 or inputs[0].type.tensor_type.elem_type != onnx.TensorProto.FLOAT or len(inputs[0].type.tensor_type.shape.dim) != 4:
        raise ValueError("Expected one float32 NCHW input; FP16 internal weights are supported")
    if len(graph.graph.output) != 1:
        raise ValueError("Select/validate a one-output relative depth export")
    manifest = {"id": model.stem, "contract": "relative-inverse-depth-nchw-v1", "file": model.name,
                "sha256": digest_file(model), "output": graph.graph.output[0].name,
                "mean": [.485, .456, .406], "std": [.229, .224, .225], "license": args.weight_license,
                "graph_optimization": args.graph_optimization,
                "provenance": "User-supplied ONNX export; normalization must be validated against its exporter"}
    destination = model.with_suffix(".json")
    destination.write_text(json.dumps(manifest, indent=2))
    print(destination)


if __name__ == "__main__":
    main()
