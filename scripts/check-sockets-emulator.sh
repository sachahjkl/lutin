#!/usr/bin/env bash
set -euo pipefail

rom=$(realpath "${1:?Supply the socket test ROM.}")
output=$(realpath -m "${2:?Supply an output directory.}")
config=${3:?Supply the emulator configuration.}
mkdir -p "$output"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy OMP_THREAD_LIMIT=1
mkdir -p "$XDG_CONFIG_HOME/melonDS" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$config" "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
chmod u+w "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
cp "$rom" "$work/sockets.nds"
chmod u+w "$work/sockets.nds"
# shellcheck disable=SC2016
timeout --kill-after=2 40 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 20 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  xdotool windowsize "$window" 512 800
  sleep 15
  magick import -window "$window" "$output/sockets.png"
  tesseract "$output/sockets.png" "$output/sockets" -l eng --psm 6 2> "$output/ocr.log"
  cat "$output/sockets.txt"
  grep -q "Socket attempt 24:" "$output/emulator.log"
  grep -q "SOCKET PASS" "$output/emulator.log"
' check-sockets-emulator "$output" "$work/sockets.nds"
