"""Create a worker environment with a matched, reproducible GPU package set."""
import argparse
import platform
from pathlib import Path
import subprocess
import sys
import venv


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("cuda", "apple", "cpu"), required=True)
    parser.add_argument("--venv", type=Path, default=Path(".venv310"))
    args = parser.parse_args()
    if not (3, 10) <= sys.version_info[:2] <= (3, 13):
        parser.error("Use Python 3.10 through 3.13 (3.11 recommended for a new checkout)")
    if args.backend == "apple" and (platform.system() != "Darwin" or platform.machine() != "arm64"):
        parser.error("Apple setup requires native arm64 Python on Apple silicon; do not run under Rosetta")
    if args.backend == "cuda" and (platform.system() not in ("Windows", "Linux") or platform.machine().lower() not in ("amd64", "x86_64")):
        parser.error("This CUDA package set targets Windows/Linux x64")
    root = Path(__file__).resolve().parents[1]
    target = args.venv.resolve()
    python = target / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(target)
    conflict = "onnxruntime" if args.backend == "cuda" else "onnxruntime-gpu"
    check = subprocess.run([str(python), "-m", "pip", "show", conflict], capture_output=True)
    if check.returncode == 0:
        parser.error(f"{conflict} is already installed in {target}. Choose a fresh --venv or uninstall it before switching backends.")
    def pip(*arguments):
        subprocess.run([str(python), "-m", "pip", *arguments], cwd=root, check=True)
    pip("install", "--upgrade", "pip")
    index = ("https://download.pytorch.org/whl/cu128" if args.backend == "cuda"
             else "https://pypi.org/simple" if args.backend == "apple"
             else "https://download.pytorch.org/whl/cpu")
    pip("install", "torch==2.7.1", "torchvision==0.22.1", "--index-url", index)
    pip("install", "-e", f".[{args.backend},video,da3,test]", "-c", str(root / "constraints" / f"{args.backend}.txt"))
    print(f"Worker ready: {python}. Run scripts/start-worker.ps1 or bash scripts/start-worker.sh.")


if __name__ == "__main__":
    main()
