#!/usr/bin/env bash
set -euo pipefail

rom=$(realpath "${1:?Supply the ROM path.}")
output=$(realpath -m "${2:?Supply the output directory.}")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$output" "$work/sd/ai-dsi/projects/1" "$work/config/melonDS" "$work/runtime"
chmod 700 "$work/runtime"
cp "$root/examples/animation.lua" "$work/sd/ai-dsi/projects/1/main.lua"
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
# The child shell expands the capture paths and display identifier.
# shellcheck disable=SC2016
timeout --kill-after=2 40 xvfb-run -a -s '-screen 0 800x1000x24' bash -euo pipefail -c '
  output=$1
  press() {
    for key in "$@"; do
      xdotool keydown "$key"
      sleep 0.12
      xdotool keyup "$key"
      sleep 0.12
    done
  }
  melonDS "$2" > "$output/emulator.log" 2>&1 &
  emulator=$!
  trap '\''kill -KILL "$emulator" 2>/dev/null || true; wait "$emulator" 2>/dev/null || true'\'' EXIT
  window=$(timeout 15 xdotool search --sync --onlyvisible --name "melonDS" | head -n 1)
  sleep 4
  xdotool windowsize "$window" 512 800
  xdotool windowmove "$window" 0 0
  xdotool windowfocus "$window"
  sleep 1
  ffmpeg -hide_banner -loglevel error -y -f x11grab -framerate 15 -video_size 512x800 -i "$DISPLAY+0,0" \
    -t 18 -c:v libx264 -preset fast -crf 30 -pix_fmt yuv420p -movflags +faststart "$output/presentation.mp4" &
  recorder=$!
  sleep 2
  press Return
  sleep 1
  press Down Down Down Down Down a
  sleep 5
  press Return
  press Down Down Down Down a
  sleep 2
  press x
  wait "$recorder"
  ffmpeg -hide_banner -loglevel error -y -i "$output/presentation.mp4" \
    -vf "fps=5,scale=256:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=48[p];[b][p]paletteuse=dither=bayer" \
    "$output/presentation.gif"
' record-presentation "$output" "$work/ai-dsi.nds"
