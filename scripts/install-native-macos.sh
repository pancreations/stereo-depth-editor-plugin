#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source_path="${1:-$project_root/build-macos/native/premiere/StereoDepth.plugin}"
if [[ ! -d "$source_path/Contents/MacOS" ]]; then
  echo 'Build the macOS plugin with both Adobe SDK roots first.' >&2; exit 1
fi
if pgrep -f '/Adobe (Premiere Pro|Media Encoder).*\.app/Contents/MacOS/' >/dev/null; then
  echo 'Close Premiere and Media Encoder before installing.' >&2; exit 1
fi
destination="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/StereoDepth"
mkdir -p "$destination"
if [[ -e "$destination/StereoDepth.plugin" ]]; then
  echo 'An installed bundle already exists. Move it to a backup location before installing.' >&2; exit 1
fi
ditto "$source_path" "$destination/StereoDepth.plugin"
echo "Installed $destination/StereoDepth.plugin. Keep the source checkout in place; restart Premiere."
