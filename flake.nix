{
  description = "Lutin: an AI coding agent for Nintendo DSi";

  inputs = {
    nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0.2605";
    git-hooks = {
      url = "https://flakehub.com/f/cachix/git-hooks.nix/0.1";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    blocksds-nix = {
      url = "github:pgattic/blocksds-nix";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
    };
    spec-kit = {
      url = "github:github/spec-kit/v1.0.12";
      flake = false;
    };
  };

  outputs = inputs: let
    system = "x86_64-linux";
    pkgs = import inputs.nixpkgs {
      inherit system;
      overlays = [inputs.blocksds-nix.overlays.default];
    };
    blocksds = pkgs.blocksdsNix.blocksdsSlim;
    networkLibraries = import ./nix/network-libraries.nix {inherit pkgs;};
    jsonSource = pkgs.runCommand "cjson-source" {} ''
      mkdir -p "$out"
      mkdir "$out/embedded"
      cp ${pkgs.cjson.src}/cJSON.c ${pkgs.cjson.src}/cJSON.h "$out/embedded/"
    '';
    luaArchive = pkgs.fetchzip {
      url = "https://www.lua.org/ftp/lua-5.4.9.tar.gz";
      hash = "sha256-t2xzwJrMSWAAsBjP7AUkE/H8QgMkBYXqtns59RqPoxU=";
    };
    luaSource = pkgs.runCommand "lua-embedded-source" {} ''
      cp -r ${luaArchive} "$out"
      chmod -R u+w "$out"
      rm "$out/src/lua.c" "$out/src/luac.c"
      sed -i 's/^#define LUA_32BITS[[:space:]]*0/#define LUA_32BITS 1/' "$out/src/luaconf.h"
    '';
    specify = pkgs.python3Packages.buildPythonApplication {
      pname = "specify-cli";
      version = "1.0.12";
      pyproject = true;
      src = inputs.spec-kit;
      build-system = [pkgs.python3Packages.hatchling];
      dependencies = with pkgs.python3Packages; [
        typer
        click
        rich
        readchar
        pyyaml
        packaging
        pathspec
        json5
      ];
      pythonImportsCheck = ["specify_cli"];
    };
    source = pkgs.lib.fileset.toSource {
      root = ./.;
      fileset = pkgs.lib.fileset.unions [./Makefile ./source];
    };
    rom = pkgs.blocksdsNix.stdenvBlocksdsSlim.mkDerivation {
      pname = "ai-dsi";
      version = "0.3.0";
      src = source;
      nativeBuildInputs = [pkgs.gnumake];
      LUA_SOURCE = luaSource;
      NETWORK_LIBRARIES = networkLibraries;
      JSON_SOURCE = "${jsonSource}/embedded";
      installPhase = ''
        runHook preInstall
        mkdir -p "$out"
        cp ai-dsi.nds "$out/"
        runHook postInstall
      '';
    };
    installationKit = import ./nix/installation-kit.nix {inherit pkgs rom;};
    releaseKit = pkgs.runCommand "lutin-sd-kit" {} ''
      mkdir -p "$out/roms/nds" "$out/ai-dsi/projects/1"
      cp ${rom}/ai-dsi.nds "$out/roms/nds/ai-dsi.nds"
      cp ${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/ai-dsi/ca.pem"
      cp ${./examples/config.json} "$out/ai-dsi/config.json"
      cp ${./examples/animation.lua} "$out/ai-dsi/projects/1/main.lua"
      cp ${./docs/usage.md} "$out/usage.md"
      cp ${./docs/install-dsi.md} "$out/install-dsi.md"
      cp ${./LICENSE} "$out/LICENSE"
      cp ${./THIRD_PARTY.md} "$out/THIRD_PARTY.md"
      test -s "$out/roms/nds/ai-dsi.nds"
      test -s "$out/ai-dsi/ca.pem"
    '';
    runtimeCheck =
      pkgs.runCommand "ai-dsi-runtime-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
          cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${./source} \
            ${./tests/runtime.c} ${./source/runtime.c} ${luaSource}/src/*.c -lm -o test-runtime
        timeout 10 ./test-runtime
          touch "$out"
      '';
    agentCheck =
      pkgs.runCommand "ai-dsi-agent-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
          cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${jsonSource}/embedded -I${./source} \
            ${./tests/agent.c} ${./tests/fat-rename.c} -Wl,--wrap=rename ${./source/agent.c} ${./source/tools.c} ${./source/workspace.c} \
            ${./source/config.c} ${./source/runtime.c} ${luaSource}/src/*.c ${jsonSource}/embedded/cJSON.c -lm -o test-agent
        timeout 10 ./test-agent
          touch "$out"
      '';
    liveAgent =
      pkgs.runCommand "ai-dsi-live-agent" {
        nativeBuildInputs = [pkgs.stdenv.cc];
        buildInputs = [pkgs.curl];
      } ''
        mkdir -p "$out/bin"
        cc -std=c11 -D_POSIX_C_SOURCE=200809L -DCJSON_NESTING_LIMIT=64 -Wall -Wextra -Werror \
          -I${luaSource}/src -I${jsonSource}/embedded -I${./source} -I${./tests/support} \
          ${./tests/live-agent.c} ${./source/agent.c} ${./source/tools.c} ${./source/workspace.c} \
          ${./source/config.c} ${./source/runtime.c} ${./source/network.c} ${./source/sse.c} ${./source/protocol.c} \
          ${luaSource}/src/*.c ${jsonSource}/embedded/cJSON.c -lcurl -lm -o "$out/bin/live-agent"
      '';
    workspaceCheck =
      pkgs.runCommand "ai-dsi-workspace-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${./source} \
          ${./tests/workspace.c} ${./source/workspace.c} \
          -Wl,--wrap=rename,--wrap=fwrite,--wrap=fclose,--wrap=unlink -o test-workspace
        timeout 10 ./test-workspace
        touch "$out"
      '';
    chatCheck =
      pkgs.runCommand "ai-dsi-chat-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} \
          ${./tests/chat.c} ${./source/chat.c} -o test-chat
        timeout 10 ./test-chat
        touch "$out"
      '';
    protocolCheck =
      pkgs.runCommand "ai-dsi-protocol-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} -I${jsonSource}/embedded \
          ${./tests/protocol.c} ${./source/protocol.c} ${jsonSource}/embedded/cJSON.c -o test-protocol
        timeout 10 ./test-protocol
        touch "$out"
      '';
    networkCheck =
      pkgs.runCommand "ai-dsi-network-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
        buildInputs = [pkgs.curl];
      } ''
        cc -std=c11 -DTEST_WIFI_CONTROL -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${./source} -I${./tests/support} -I${jsonSource}/embedded \
          ${./tests/network.c} ${./source/network.c} ${./source/protocol.c} ${./source/sse.c} \
          ${jsonSource}/embedded/cJSON.c -Wl,--wrap=time -lcurl -o test-network
        timeout 10 ./test-network
        touch "$out"
      '';
    sdFormatLinux = import ./nix/sd-format-linux.nix {inherit pkgs;};
    emulatorTools = with pkgs; [melonds xvfb-run xdotool imagemagick tesseract];
    schedulerRom = pkgs.blocksdsNix.stdenvBlocksdsSlim.mkDerivation {
      pname = "ai-dsi-scheduler-test";
      version = "1";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./tests/scheduler ./source/platform.h ./source/input.h ./source/platform_input.h];
      };
      nativeBuildInputs = [pkgs.gnumake];
      buildPhase = "make -C tests/scheduler";
      installPhase = ''
        mkdir -p "$out"
        cp tests/scheduler/scheduler.nds "$out/"
      '';
    };
    emulatorCheck =
      pkgs.runCommand "ai-dsi-emulator-check" {
        nativeBuildInputs = emulatorTools;
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/check-emulator.sh} ${rom}/ai-dsi.nds "$out" ${./tests/melonds.toml} ${schedulerRom}/scheduler.nds
      '';
    preCommitCheck = inputs.git-hooks.lib.${system}.run {
      package = pkgs.prek;
      src = ./.;
      excludes = [
        "^\\.specify/(scripts|templates|integrations|workflows)/"
        "^\\.opencode/commands/speckit\\."
      ];
      hooks = {
        alejandra.enable = true;
        deadnix.enable = true;
        statix.enable = true;
        clang-format = {
          enable = true;
          types_or = ["c" "c++"];
        };
        shellcheck.enable = true;
        shfmt.enable = true;
        taplo.enable = true;
        stylua.enable = true;
        prettier = {
          enable = true;
          types_or = ["markdown" "json" "yaml"];
        };
        check-merge-conflicts.enable = true;
        check-added-large-files = {
          enable = true;
          excludes = ["^\\.project/image\\.png$"];
        };
        check-json.enable = true;
        check-yaml.enable = true;
        end-of-file-fixer.enable = true;
        trim-trailing-whitespace.enable = true;
      };
    };
  in {
    packages.${system} = {
      default = rom;
      inherit rom specify;
      installation-kit = installationKit;
      release-kit = releaseKit;
      sd-format-linux = sdFormatLinux;
      emulator-check = emulatorCheck;
      live-agent = liveAgent;
    };
    checks.${system} = {
      workflows = pkgs.runCommand "lutin-workflow-check" {nativeBuildInputs = [pkgs.actionlint pkgs.shellcheck];} ''
        actionlint ${./.github/workflows/ci.yml}
        touch "$out"
      '';
      input = pkgs.runCommand "lutin-input-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} ${./tests/input.c} -o test-input
        ./test-input
        touch "$out"
      '';
      config = pkgs.runCommand "lutin-config-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} -I${jsonSource}/embedded \
          ${./tests/config.c} ${./source/config.c} ${jsonSource}/embedded/cJSON.c -o test-config
        ./test-config
        touch "$out"
      '';
      protocol = protocolCheck;
      network = networkCheck;
      chat = chatCheck;
      workspace = workspaceCheck;
      inherit rom specify;
      installation-kit = installationKit;
      release-kit = releaseKit;
      sd-format-linux = sdFormatLinux;
      emulator = emulatorCheck;
      pre-commit = preCommitCheck;
      runtime = runtimeCheck;
      agent = agentCheck;
      live-agent-build = liveAgent;
      sse = pkgs.runCommand "ai-dsi-sse-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        cc -std=c11 -Wall -Wextra -Werror -I${./source} ${./source/sse.c} ${./tests/sse.c} -o test-sse
        ./test-sse
        touch "$out"
      '';
    };
    formatter.${system} = pkgs.alejandra;
    devShells.${system}.default = pkgs.mkShell {
      packages =
        preCommitCheck.enabledPackages
        ++ emulatorTools
        ++ [
          blocksds
          specify
          sdFormatLinux
          pkgs.gnumake
          pkgs.git
          pkgs.curl
          pkgs.jq
          pkgs._7zz
          pkgs.unzip
          pkgs.zip
          pkgs.ffmpeg-full
          pkgs.dosfstools
          pkgs.f3
        ];
      inherit (blocksds.passthru) WONDERFUL_TOOLCHAIN BLOCKSDS BLOCKSDSEXT;
      LUA_SOURCE = luaSource;
      NETWORK_LIBRARIES = networkLibraries;
      JSON_SOURCE = "${jsonSource}/embedded";
      inherit (preCommitCheck) shellHook;
    };
  };
}
