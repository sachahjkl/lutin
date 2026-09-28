{pkgs}: let
  sources = builtins.fromJSON (builtins.readFile ./installation-sources.json);
in
  pkgs.stdenv.mkDerivation {
    pname = "sd-format-linux";
    version = "0.2.0";
    src = pkgs.fetchurl (sources."sdFormatLinux.7z" // {name = "sdFormatLinux.7z";});
    nativeBuildInputs = [pkgs._7zz pkgs.autoPatchelfHook];
    buildInputs = [pkgs.stdenv.cc.cc.lib];
    unpackPhase = ''
      7zz x "$src"
    '';
    dontConfigure = true;
    dontBuild = true;
    installPhase = ''
      install -Dm755 sdFormatLinux "$out/bin/sdFormatLinux"
      install -Dm644 LICENSE.txt "$out/share/licenses/sd-format-linux/LICENSE.txt"
    '';
  }
