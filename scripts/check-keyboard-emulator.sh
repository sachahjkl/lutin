#!/usr/bin/env bash
set -euo pipefail
rom=$(realpath "${1:?Supply the keyboard test ROM.}")
output=$(realpath -m "${2:?Supply an output directory.}")
config=${3:?Supply emulator configuration.}
catalog=${4:?Supply model catalog.}
mkdir -p "$output"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy OMP_THREAD_LIMIT=1
mkdir -p "$XDG_CONFIG_HOME/melonDS" "$XDG_RUNTIME_DIR" "$work/sd/lutin/projects/1"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$config" "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
chmod u+w "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
cp "$catalog" "$work/sd/lutin/models.json"
cat >"$work/sd/lutin/projects/1/session-1.json" <<'EOF'
{"history":[{"role":"user","content":"Été : é à ç œ Noël"}],"queue":[],"journal":{}}
EOF
cat >>"$XDG_CONFIG_HOME/melonDS/melonDS.toml" <<EOF

[DLDI]
Enable = true
ImagePath = "$work/dldi.bin"
ImageSize = 0
ReadOnly = false
FolderSync = true
FolderPath = "$work/sd"
EOF
cp "$rom" "$work/keyboard.nds"
chmod u+w "$work/keyboard.nds"
# shellcheck disable=SC2016
timeout --kill-after=2 40 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 20 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  xdotool windowsize "$window" 512 800
  xdotool windowfocus "$window"
  sleep 4
  xdotool windowsize "$window" 512 800
  sleep 1
  magick import -window "$window" "$output/accents.png"
  xdotool mousemove --window "$window" 90 710 mousedown 1
  sleep 1
  magick import -window "$window" "$output/pressed.png"
  xdotool mouseup 1
  grep -q "KEYBOARD TILES PASS initialization" "$output/emulator.log"
  grep -q "KEYBOARD TILES PASS pressed" "$output/emulator.log"
  if grep -q "KEYBOARD TILES FAIL" "$output/emulator.log"; then exit 1; fi
' check-keyboard-emulator "$output" "$work/keyboard.nds"
