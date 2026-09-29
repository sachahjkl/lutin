#!/usr/bin/env bash
set -euo pipefail
rom=$(realpath "${1:?Supply the hardware test ROM.}")
output=$(realpath -m "${2:?Supply an output directory.}")
config=${3:?Supply emulator configuration.}
mkdir -p "$output"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export HOME="$work" XDG_CONFIG_HOME="$work/config" XDG_RUNTIME_DIR="$work/runtime"
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy OMP_THREAD_LIMIT=1
mkdir -p "$XDG_CONFIG_HOME/melonDS" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$config" "$XDG_CONFIG_HOME/melonDS/melonDS.toml"
cp "$rom" "$work/hardware.nds"
chmod -R u+w "$work"
# shellcheck disable=SC2016
timeout --kill-after=2 60 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  for ((i=0;i<100;i++)); do
    kill -0 "$emulator"
    if grep -q "GPU ALL PASS" "$output/emulator.log"; then
      grep "GPU .*PASS" "$output/emulator.log"
      exit 0
    fi
    sleep 0.5
  done
  cat "$output/emulator.log"
  exit 1
' check-hardware "$output" "$work/hardware.nds"
