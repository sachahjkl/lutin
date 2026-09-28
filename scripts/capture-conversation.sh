#!/usr/bin/env bash
set -euo pipefail

rom=$(realpath "${1:?Supply the ROM path.}")
session=$(realpath "${2:?Supply the conversation JSON file.}")
output=$(realpath -m "${3:?Supply the capture directory.}")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$output" "$work/sd/ai-dsi/projects/1" "$work/config/melonDS" "$work/runtime"
chmod 700 "$work/runtime"
cp "$session" "$work/sd/ai-dsi/projects/1/session-1.json"
cp "$rom" "$work/ai-dsi.nds"
chmod u+w "$work/ai-dsi.nds"
cp "$root/tests/melonds.toml" "$work/config/melonDS/melonDS.toml"
chmod u+w "$work/config/melonDS/melonDS.toml"
cat >>"$work/config/melonDS/melonDS.toml" <<EOF

[DLDI]
Enable = true
ImagePath = "$work/dldi.bin"
ImageSize = 0
ReadOnly = false
FolderSync = true
FolderPath = "$work/sd"
EOF
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy
# The child process uses paths supplied as arguments.
# shellcheck disable=SC2016
timeout --kill-after=2 30 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 15 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 5
  xdotool windowsize "$window" 512 800
  sleep 0.5
  magick import -window "$window" "$output/conversation.png"
  tesseract "$output/conversation.png" "$output/conversation" -l eng --psm 6 2> "$output/ocr.log"
  cat "$output/conversation.txt"
  grep -Eiq "^YOU$" "$output/conversation.txt"
  grep -Eiq "^AGENT$" "$output/conversation.txt"
' capture-conversation "$output" "$work/ai-dsi.nds"
