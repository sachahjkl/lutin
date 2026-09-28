#!/usr/bin/env bash
set -euo pipefail

rom=$(realpath "${1:?Supply the DNS fault-injection ROM.}")
output=$(realpath -m "${2:?Supply an output directory.}")
config=${3:?Supply the emulator configuration.}
catalog=${4:?Supply the model catalog.}
mkdir -p "$output"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy OMP_THREAD_LIMIT=1
mkdir -p "$XDG_CONFIG_HOME/melonDS" "$XDG_RUNTIME_DIR" "$work/sd/lutin"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$config" "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
chmod u+w "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
cp "$catalog" "$work/sd/lutin/models.json"
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

# The child shell expands these variables during the test.
# shellcheck disable=SC2016
timeout --kill-after=2 90 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  press() {
    for key in "$@"; do
      xdotool keydown "$key"
      sleep 0.12
      xdotool keyup "$key"
      sleep 0.12
    done
  }
  capture() {
    magick import -window "$window" "$output/$1.png"
    tesseract "$output/$1.png" "$output/$1" -l eng --psm 6 2>> "$output/ocr.log"
    cat "$output/$1.txt"
  }
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 20 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 3
  xdotool windowsize "$window" 512 800
  xdotool windowfocus "$window"
  # Update model catalog is two entries before the menu wraps to Keyboard.
  press Return Up Up a
  sleep 1
  capture pending
  grep -Eiq "B Cancel update" "$output/pending.txt"
  grep -q "DNS fixture enter 1" "$output/emulator.log"
  if grep -q "DNS fixture exit 1" "$output/emulator.log"; then
    echo "DNS returned before input was tested" >&2
    exit 1
  fi
  press b
  sleep 0.5
  capture cancelled
  grep -Eiq "update cancelled" "$output/cancelled.txt"
  grep -Eiq "DNS" "$output/pending.txt"
  press Return
  sleep 0.5
  capture menu
  grep -Eiq "Select.*Back" "$output/menu.txt"
  grep -Eiq "Stop agent" "$output/menu.txt"
  if grep -q "DNS fixture exit 1" "$output/emulator.log"; then
    echo "DNS returned before menu input was tested" >&2
    exit 1
  fi
  press a
  sleep 0.5
  capture worker-pending
  grep -Eiq "DNS worker unavailable" "$output/worker-pending.txt"
  # Let the cancelled resolver finish. Its result must not change this request.
  deadline=$((SECONDS + 50))
  until grep -q "DNS fixture exit 1" "$output/emulator.log"; do
    (( SECONDS < deadline )) || exit 1
    sleep 0.2
  done
  sleep 0.5
  capture late-result
  grep -q "DNS fixture exit 1" "$output/emulator.log"
  grep -Eiq "DNS worker unavailable" "$output/late-result.txt"
  press Return a
  sleep 1
  capture retry
  grep -Eiq "DNS lookup failed" "$output/retry.txt"
  grep -q "DNS fixture exit 2" "$output/emulator.log"
  echo "PASS: stalled DNS permits cancellation and menu input; late results are isolated; retry completes."
' check-network-emulator "$output" "$work/lutin.nds"
