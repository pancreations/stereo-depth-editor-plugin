#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
device="${S3D_DEVICE:-auto}"
if [[ "${1:-}" == "--setup" ]]; then
  backend=apple
  [[ "$(uname -s)" == Darwin ]] || backend=cpu
  python3 tools/setup_worker.py --backend "$backend"
  shift
fi
python_path="${S3D_PYTHON:-$project_root/.venv310/bin/python}"
if [[ ! -x "$python_path" ]]; then
  echo 'Create the environment with python3 tools/setup_worker.py --backend apple (or cpu/cuda).' >&2
  exit 1
fi
exec "$python_path" -m stereodepth_worker.server --device "$device" --connection runtime/connection.json "$@"
