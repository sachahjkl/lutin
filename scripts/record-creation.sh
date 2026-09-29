#!/usr/bin/env bash
set -euo pipefail
rom=$(realpath "${1:?Supply the observed replay ROM.}")
fixtures=$(realpath "${2:?Supply recorded session fixtures.}")
output=$(realpath -m "${3:?Supply an output directory.}")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
config=${4:-$root/tests/melonds.toml}
catalog=${5:-$root/catalog/models.json}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$output" "$work/sd/lutin/projects/1" "$work/config/melonDS" "$work/runtime"
if [[ -d "$fixtures/project" ]]; then
	cp -R "$fixtures/project/." "$work/sd/lutin/projects/1/"
fi
chmod 700 "$work/runtime"
cp "$fixtures/session.json" "$work/sd/lutin/projects/1/session-1.json"
cp "$fixtures/responses.json" "$work/sd/lutin/replay.json"
cp "$fixtures/expected.json" "$work/sd/lutin/expected.json"
if [[ ${LUTIN_TEST_FREEZE:-0} == 1 ]]; then
	touch "$work/sd/lutin/freeze-after-done"
fi
if [[ ${LUTIN_TEST_INSPECTION:-0} == 1 ]]; then
	touch "$work/sd/lutin/fail-inspection-on-a"
fi
cp "$catalog" "$work/sd/lutin/models.json"
cp "$config" "$work/config/melonDS/melonDS.toml"
cp "$rom" "$work/creation.nds"
chmod -R u+w "$work"
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
export QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy OMP_THREAD_LIMIT=1
# shellcheck disable=SC2016
timeout --kill-after=2 360 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  press() { xdotool key --clearmodifiers --delay 150 "$@"; sleep 0.3; }
  stdbuf -oL -eL melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''if test -n "${recorder:-}"; then kill -INT "$recorder" 2>/dev/null || true; fi; kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 20 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 4
  xdotool windowsize "$window" 512 800
  xdotool windowmove "$window" 0 0
  xdotool windowfocus "$window"
  sleep 1
  ffmpeg -hide_banner -loglevel error -y -f x11grab -framerate 15 -video_size 512x800 -i "$DISPLAY+0,0" \
    -t 330 -c:v libx264 -threads 2 -preset fast -crf 24 -pix_fmt yuv420p "$output/session.mp4" &
  recorder=$!
  press Return Down Down Down a
  sleep 1
  press a
  press Return Up Up a
  for ((i=0;i<560;i++)); do
    kill -0 "$emulator"
    if grep -q "E2E DONE" "$output/emulator.log"; then break; fi
    sleep 0.5
  done
  magick import -window "$window" "$output/session.png"
  grep "E2E " "$output/emulator.log"
  grep -q "E2E DONE PASS" "$output/emulator.log"
  press Return Down a
  xdotool keydown Right
  sleep 0.5
  xdotool keyup Right
  press a
  xdotool keydown Up
  sleep 1
  xdotool keyup Up
  magick import -window "$window" "$output/playing.png"
  sleep 2
  for ((i=0;i<40;i++)); do
    kill -0 "$emulator"
    if grep -q "E2E PLAY FAIL" "$output/emulator.log"; then break; fi
    if grep -q "E2E PLAY PASS" "$output/emulator.log"; then break; fi
    sleep 0.5
  done
  if ! grep -q "E2E PLAY PASS" "$output/emulator.log"; then
    echo "E2E gameplay did not advance with the expected input changes." >&2
    exit 1
  fi
  if grep -q "E2E PLAY FAIL" "$output/emulator.log"; then exit 1; fi
  kill -INT "$recorder"
  wait "$recorder" || test "$?" -eq 255
' record-creation "$output" "$work/creation.nds"
