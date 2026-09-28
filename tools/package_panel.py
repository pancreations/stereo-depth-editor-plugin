"""Create the local Premiere-only CCX package (ZIP, no credentials/checkpoints)."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import json

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "uxp/manifest.json").read_text())
assert isinstance(manifest["host"], dict) and manifest["host"]["app"] == "premierepro"
output = root / "artifacts/StereoDepth-premierepro.ccx"
output.parent.mkdir(exist_ok=True)
with ZipFile(output, "w", ZIP_DEFLATED) as package:
    for name in ("manifest.json", "index.html", "style.css", "panel.js", "premiere.js"):
        package.write(root / "uxp" / name, name)
print(output)
