{
  pkgs,
  rom,
}: let
  sources = builtins.fromJSON (builtins.readFile ./installation-sources.json);
  downloads = pkgs.lib.mapAttrs (name: source: pkgs.fetchurl (source // {inherit name;})) sources;
in
  pkgs.runCommand "ai-dsi-installation-kit" {
    nativeBuildInputs = [pkgs._7zz];
  } ''
    mkdir -p "$out"/{downloads,01-backup,02-menu,emulation,memory-pit/{facebook,no-facebook}}
    ${pkgs.lib.concatStringsSep "\n" (pkgs.lib.mapAttrsToList (name: file: ''
        cp ${file} "$out/downloads/${name}"
      '')
      downloads)}
    cp ${./installation-sources.json} "$out/downloads/sources.json"
    cp ${../docs/install-dsi.md} "$out/install-dsi.md"
    cp ${../docs/usage.md} "$out/usage.md"
    cp ${../LICENSE} "$out/LICENSE"
    cp ${../THIRD_PARTY.md} "$out/THIRD_PARTY.md"
    cp ${downloads."dumpTool.nds"} "$out/01-backup/boot.nds"
    cp ${downloads."pit-facebook.bin"} "$out/memory-pit/facebook/pit.bin"
    cp ${downloads."pit-no-facebook.bin"} "$out/memory-pit/no-facebook/pit.bin"
    7zz x ${downloads."TWiLightMenu-DSi.7z"} -otwilight
    cp -r twilight/_nds twilight/roms twilight/BOOT.NDS "$out/02-menu/"
    cp ${downloads."unlaunch-installer.dsi"} "$out/02-menu/unlaunch-installer.dsi"
    cp ${rom}/ai-dsi.nds "$out/02-menu/roms/nds/ai-dsi.nds"
    mkdir -p "$out/02-menu/ai-dsi"
    cp ${../examples/config.json} "$out/02-menu/ai-dsi/config.json"
    cp ${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/02-menu/ai-dsi/ca.pem"
    mkdir -p "$out/02-menu/ai-dsi/projects/1"
    cp ${../examples/animation.lua} "$out/02-menu/ai-dsi/projects/1/main.lua"
    7zz x ${downloads."dsibiosdumper.7z"} -o"$out/emulation"
    test -s "$out/02-menu/BOOT.NDS"
    test -s "$out/02-menu/_nds/nds-bootstrap-hb-release.nds"
    test -s "$out/emulation/dsibiosdumper.nds"
    cd "$out/downloads"
    sha256sum -- *.7z *.nds *.dsi *.bin > SHA256SUMS
    sha256sum --check SHA256SUMS
  ''
