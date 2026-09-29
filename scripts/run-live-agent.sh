#!/usr/bin/env bash
set -euo pipefail
output=$(realpath -m "${1:?Supply an empty output directory.}")
model=${2:?Supply a provider-qualified model ID.}
prompt=$(realpath "${3:?Supply a prompt file.}")
previous=${4:-}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${LUTIN_LIVE_AGENT:-$root/result-live-agent/bin/live-agent}
if [[ -d $output ]] && [[ -n $(find "$output" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
	echo "Output directory must be empty." >&2
	exit 1
fi
umask 077
work=$(mktemp -d "${TMPDIR:-/tmp/opencode}/lutin-live.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/keys" "$output"
if [[ -n $previous ]]; then
	cp -R "$previous/projects" "$work/"
	cp -R "$previous/projects/1" "$output/initial-project"
fi
sops --decrypt --extract '["opencode-go"]' "$root/secrets/testing.json" >"$work/keys/opencode-go"
cp "$root/catalog/models.json" "$work/models.json"
cp "${SSL_CERT_FILE:-/etc/ssl/certs/ca-certificates.crt}" "$work/ca.pem"
status=0
"$binary" "$work" "$model" "$(cat "$prompt")" >"$output/run.log" 2>&1 || status=$?
for item in projects responses.json result.ppm; do
	if [[ -e "$work/$item" ]]; then
		cp -R "$work/$item" "$output/"
	fi
done
cp "$prompt" "$output/prompt.txt"
cat "$output/run.log"
exit "$status"
