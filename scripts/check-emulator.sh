#!/usr/bin/env bash
set -euo pipefail

rom=$(realpath "${1:?Supply the ROM path.}")
output=$(realpath -m "${2:?Supply an empty output directory.}")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
config=${3:-$root/tests/melonds.toml}
scheduler=$(realpath "${4:?Supply the scheduler test ROM path.}")
fixtures=${5:-$root/examples}
catalog=${6:-$root/catalog/models.json}
mkdir -p "$output"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy
export OMP_THREAD_LIMIT=1
mkdir -p "$XDG_CONFIG_HOME/melonDS" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$config" "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
chmod u+w "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
mkdir -p "$work/sd/lutin/projects/1"
cp "$catalog" "$work/sd/lutin/models.json"
cp "$fixtures/media.lua" "$work/sd/lutin/projects/1/main.lua"
cp "$fixtures/hero.lua" "$fixtures/sounds.lua" "$work/sd/lutin/projects/1/"
chmod -R u+w "$work/sd"
cat >>"$XDG_CONFIG_HOME/melonDS/melonDS.toml" <<EOF

[DLDI]
Enable = true
ImagePath = "$work/dldi.bin"
ImageSize = 0
ReadOnly = false
FolderSync = true
FolderPath = "$work/sd"
EOF
cp "$rom" "$work/lutin.nds"
chmod u+w "$work/lutin.nds"
cp "$scheduler" "$work/scheduler.nds"
chmod u+w "$work/scheduler.nds"

# The child shell expands these variables during the test.
# shellcheck disable=SC2016
timeout --kill-after=2 120 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  press() {
    for key in "$@"; do
      xdotool keydown "$key"
      sleep 0.12
      xdotool keyup "$key"
      sleep 0.12
    done
  }
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 20 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 3
  xdotool windowsize "$window" 512 800
  sleep 1
  magick import -window "$window" "$output/boot.png"
  tesseract "$output/boot.png" "$output/boot" -l eng --psm 6 2> "$output/ocr.log"
  cat "$output/boot.txt"
  grep -Eiq "CHAT" "$output/boot.txt"
  grep -Eiq "QUEUE.*Send" "$output/boot.txt"
  xdotool windowfocus "$window"
  press Return
  sleep 0.3
  magick import -window "$window" "$output/menu.png"
  tesseract "$output/menu.png" "$output/menu" -l eng --psm 6 2>> "$output/ocr.log"
  grep -Eiq "MENU" "$output/menu.txt"
  press Down Down Down Down Down a
  sleep 2
  magick import -window "$window" "$output/running.png"
  tesseract "$output/running.png" "$output/running" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/running.txt"
  grep -Eq "RUN" "$output/running.txt"
  grep -Eiq "SPRITES.*AUDIO" "$output/running.txt"
  press Return
  sleep 0.3
  press Up Up Up a
  sleep 0.5
  press Return
  sleep 0.3
  magick import -window "$window" "$output/play-menu.png"
  tesseract "$output/play-menu.png" "$output/play-menu" -l eng --psm 6 2>> "$output/ocr.log"
  grep -Eiq "MENU" "$output/play-menu.txt"
  press Up Up a
  xdotool keydown l
  sleep 0.2
  xdotool keyup l
  sleep 1
  magick import -window "$window" "$output/keyboard.png"
  tesseract "$output/keyboard.png" "$output/keyboard" -l eng --psm 6 2>> "$output/ocr.log"
  grep -Eiq "QUEUE.*Send" "$output/keyboard.txt"
  press Return
  press Down Down Down Down Down Down Down Down Down Down Down Down a
  sleep 0.5
  magick import -window "$window" "$output/models.png"
  tesseract "$output/models.png" "$output/models" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/models.txt"
  grep -Eiq "MODEL.*Select" "$output/models.txt"
  press Return Up Up Up Up Up Up a
  press Return Down Down Down Down a
  press x
  sleep 1
  magick import -window "$window" "$output/projects.png"
  tesseract "$output/projects.png" "$output/projects" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/projects.txt"
  grep -Eiq "Project 2" "$output/projects.txt"
  press Return Down Down Down Down Down a
  sleep 1
  magick import -window "$window" "$output/catalog.png"
  tesseract "$output/catalog.png" "$output/catalog" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/catalog.txt"
  grep -Eiq "catalog reloaded" "$output/catalog.txt"
  kill -KILL "$emulator"
  wait "$emulator" 2>/dev/null || true
  stdbuf -oL -eL melonDS "$3" > "$output/scheduler.log" 2>&1 &
  emulator=$!
  window=$(timeout 10 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 3
  xdotool windowsize "$window" 512 800
  sleep 5
  magick import -window "$window" "$output/scheduler.png"
  tesseract "$output/scheduler.png" "$output/scheduler" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/scheduler.txt"
  grep -Eq "^PASS([[:space:]]|$)" "$output/scheduler.txt"
  xdotool windowfocus "$window"
  press Return
  sleep 0.2
  press a
  sleep 3
  magick import -window "$window" "$output/input.png"
  tesseract "$output/input.png" "$output/input" -l eng --psm 6 2>> "$output/ocr.log"
  cat "$output/input.txt"
  grep -Eq "INPUT PASS" "$output/input.txt"
' check-emulator "$output" "$work/lutin.nds" "$work/scheduler.nds"
