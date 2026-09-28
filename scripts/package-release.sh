#!/usr/bin/env bash
set -euo pipefail

output=$(realpath -m "${1:?Supply an output directory.}")
version=${2:?Supply a release version.}
if [[ ! $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "Use a version such as v0.3.0." >&2
	exit 2
fi
mkdir -p "$output"
rom=$(nix build .#rom --no-link --print-out-paths)
kit=$(nix build .#release-kit --no-link --print-out-paths)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -a "$kit/." "$work/"
chmod -R u+rwX "$work"
install -m 644 "$rom/lutin.nds" "$output/lutin-$version.nds"
rm -f "$output/lutin-$version-sd.zip"
(
	cd "$work"
	zip -q -r "$output/lutin-$version-sd.zip" .
)
(
	cd "$output"
	sha256sum "lutin-$version.nds" "lutin-$version-sd.zip" >SHA256SUMS
	sha256sum --check SHA256SUMS
)
