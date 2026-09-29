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
      pname = "lutin";
      version = "0.7.0";
      src = source;
      nativeBuildInputs = [pkgs.gnumake];
      LUA_SOURCE = luaSource;
      NETWORK_LIBRARIES = networkLibraries;
      JSON_SOURCE = "${jsonSource}/embedded";
      installPhase = ''
        runHook preInstall
        mkdir -p "$out"
        cp lutin.nds "$out/"
        runHook postInstall
      '';
    };
    reverseFind = pkgs.writeShellScriptBin "find" ''
      set -euo pipefail
      ${pkgs.findutils}/bin/find "$@" | ${pkgs.coreutils}/bin/sort -r
    '';
    reproducibleRomCheck = rom.overrideAttrs (_: {
      pname = "lutin-rom-reproducibility";
      preBuild = ''export PATH="${reverseFind}/bin:$PATH"'';
      installPhase = ''
        cmp lutin.nds ${rom}/lutin.nds
        touch "$out"
      '';
    });
    presentationRom = rom.overrideAttrs (_: {
      pname = "lutin-presentation";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/presentation/network.c];
      };
      makeFlags = ["NETWORK_SOURCE=tests/presentation/network.c"];
    });
    creationReplayRom = presentationRom.overrideAttrs (_: {
      pname = "lutin-creation-replay";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/presentation/network.c ./tests/demos/observe.c];
      };
      postPatch = "cp tests/demos/observe.c source/observe-test.c";
      LDFLAGS = "-Wl,--wrap=tools_call,--wrap=agent_tick,--wrap=runtime_frame";
    });
    creationReplayCheck = name: fixtures:
      pkgs.runCommand "lutin-${name}-e2e" {
        nativeBuildInputs = emulatorTools ++ [pkgs.ffmpeg-full];
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/record-creation.sh} ${creationReplayRom}/lutin.nds ${fixtures} "$out" ${./tests/melonds.toml} ${./catalog/models.json}
      '';
    hardwareRom = rom.overrideAttrs (_: {
      pname = "lutin-hardware-test";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/hardware.c];
      };
      postPatch = "cp tests/hardware.c source/hardware-test.c";
      LDFLAGS = "-Wl,--wrap=runtime_set_font";
    });
    installationKit = import ./nix/installation-kit.nix {inherit pkgs rom;};
    releaseKit = pkgs.runCommand "lutin-sd-kit" {} ''
      mkdir -p "$out/roms/nds" "$out/lutin/projects/1" "$out/lutin/keys"
      cp ${rom}/lutin.nds "$out/roms/nds/lutin.nds"
      cp ${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/lutin/ca.pem"
      cp ${./examples/config.json} "$out/lutin/config.json"
      cp ${./catalog/models.json} "$out/lutin/models.json"
      cp ${./examples/media.lua} "$out/lutin/projects/1/main.lua"
      cp ${./examples/hero.lua} "$out/lutin/projects/1/hero.lua"
      cp ${./examples/sounds.lua} "$out/lutin/projects/1/sounds.lua"
      cp ${./docs/usage.md} "$out/usage.md"
      cp ${./docs/install-dsi.md} "$out/install-dsi.md"
      cp ${./LICENSE} "$out/LICENSE"
      cp ${./THIRD_PARTY.md} "$out/THIRD_PARTY.md"
      test -s "$out/roms/nds/lutin.nds"
      test -s "$out/lutin/ca.pem"
    '';
    runtimeCheck =
      pkgs.runCommand "lutin-runtime-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
          cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${./source} -I${jsonSource}/embedded \
            ${./tests/runtime.c} ${./source/runtime.c} ${jsonSource}/embedded/cJSON.c ${luaSource}/src/*.c -lm -o test-runtime
        timeout 10 ./test-runtime
          touch "$out"
      '';
    agentCheck =
      pkgs.runCommand "lutin-agent-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
        buildInputs = [pkgs.zlib];
      } ''
          cc -std=c11 -DLUA_USE_APICHECK -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${jsonSource}/embedded -I${./source} \
            ${./tests/agent.c} ${./tests/fat-rename.c} -Wl,--wrap=rename ${./source/agent.c} ${./source/tools.c} ${./source/image.c} ${./source/workspace.c} \
            ${./source/backend.c} ${./source/config.c} ${./source/runtime.c} ${luaSource}/src/*.c ${jsonSource}/embedded/cJSON.c -lz -lm -o test-agent
        cp ${./tests/catalog.json} models.json
        timeout 10 ./test-agent
          touch "$out"
      '';
    liveAgent =
      pkgs.runCommand "lutin-live-agent" {
        nativeBuildInputs = [pkgs.stdenv.cc pkgs.binutils pkgs.xxd];
        buildInputs = [pkgs.curl pkgs.zlib];
      } ''
        mkdir -p "$out/bin"
        ar p ${blocksds}/opt/wonderful/thirdparty/blocksds/core/libs/libnds/lib/libnds9.a default_font.png.o > font.o
        objcopy -I elf32-little -O binary font.o font.bin
        xxd -i -n default_font font.bin > font.h
        cc -std=c11 -D_POSIX_C_SOURCE=200809L -DCJSON_NESTING_LIMIT=64 -Wall -Wextra -Werror \
          -I. -I${luaSource}/src -I${jsonSource}/embedded -I${./source} -I${./tests/support} \
          ${./tests/live-agent.c} ${./source/agent.c} ${./source/tools.c} ${./source/image.c} ${./source/workspace.c} \
          ${./source/backend.c} ${./source/config.c} ${./source/runtime.c} ${./source/network.c} ${./source/resolver.c} ${./source/sse.c} ${./source/protocol.c} \
          ${luaSource}/src/*.c ${jsonSource}/embedded/cJSON.c -Wl,--wrap=network_start -pthread -lcurl -lz -lm -o "$out/bin/live-agent"
      '';
    workspaceCheck =
      pkgs.runCommand "lutin-workspace-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${./source} -I${jsonSource}/embedded \
          ${./tests/workspace.c} ${./source/workspace.c} ${jsonSource}/embedded/cJSON.c \
          -Wl,--wrap=rename,--wrap=fwrite,--wrap=fclose,--wrap=unlink -o test-workspace
        timeout 10 ./test-workspace
        touch "$out"
      '';
    chatCheck =
      pkgs.runCommand "lutin-chat-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} \
          ${./tests/chat.c} ${./source/chat.c} -o test-chat
        timeout 10 ./test-chat
        touch "$out"
      '';
    protocolCheck =
      pkgs.runCommand "lutin-protocol-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
      } ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} -I${jsonSource}/embedded \
          ${./tests/protocol.c} ${./source/protocol.c} ${jsonSource}/embedded/cJSON.c -o test-protocol
        timeout 10 ./test-protocol
        touch "$out"
      '';
    networkCheck =
      pkgs.runCommand "lutin-network-check" {
        nativeBuildInputs = [pkgs.stdenv.cc];
        buildInputs = [pkgs.curl];
      } ''
        cc -std=c11 -D_POSIX_C_SOURCE=200809L -DTEST_WIFI_CONTROL -Wall -Wextra -Werror -fsanitize=address,undefined \
          -I${./source} -I${./tests/support} -I${jsonSource}/embedded \
          ${./tests/network.c} ${./source/backend.c} ${./source/network.c} ${./source/resolver.c} ${./source/protocol.c} ${./source/sse.c} \
          ${jsonSource}/embedded/cJSON.c -Wl,--wrap=time,--wrap=getaddrinfo -pthread -lcurl -o test-network
        cp ${./tests/catalog.json} models.json
        timeout 10 ./test-network
        touch "$out"
      '';
    sdFormatLinux = import ./nix/sd-format-linux.nix {inherit pkgs;};
    emulatorTools = with pkgs; [melonds xvfb-run xdotool imagemagick tesseract];
    schedulerRom = pkgs.blocksdsNix.stdenvBlocksdsSlim.mkDerivation {
      pname = "lutin-scheduler-test";
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
      pkgs.runCommand "lutin-emulator-check" {
        nativeBuildInputs = emulatorTools;
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/check-emulator.sh} ${rom}/lutin.nds "$out" ${./tests/melonds.toml} ${schedulerRom}/scheduler.nds ${./examples} ${./catalog/models.json}
      '';
    networkStallRom = rom.overrideAttrs (_: {
      pname = "lutin-network-stall";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/network-stall.c];
      };
      postPatch = "cp tests/network-stall.c source/network-stall.c";
      LDFLAGS = "-Wl,--wrap=Wifi_InitDefault,--wrap=Wifi_AutoConnect,--wrap=Wifi_AssocStatus,--wrap=getaddrinfo";
    });
    networkEmulatorCheck =
      pkgs.runCommand "lutin-network-emulator-check" {
        nativeBuildInputs = emulatorTools;
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/check-network-emulator.sh} ${networkStallRom}/lutin.nds "$out" ${./tests/melonds.toml} ${./tests/catalog.json}
      '';
    socketTestRom = rom.overrideAttrs (_: {
      pname = "lutin-network-sockets";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/network-sockets.c];
      };
      postPatch = "cp tests/network-sockets.c source/main.c";
      LDFLAGS = "-Wl,--wrap=Wifi_AssocStatus,--wrap=getaddrinfo,--wrap=connect";
    });
    keyboardTestRom = rom.overrideAttrs (_: {
      pname = "lutin-keyboard-test";
      src = pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [./Makefile ./source ./tests/keyboard.c];
      };
      postPatch = "cp tests/keyboard.c source/keyboard-test.c";
      LDFLAGS = "-Wl,--wrap=keyboardInit_call,--wrap=keyboardUpdate";
    });
    keyboardEmulatorCheck =
      pkgs.runCommand "lutin-keyboard-emulator-check" {
        nativeBuildInputs = emulatorTools;
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/check-keyboard-emulator.sh} ${keyboardTestRom}/lutin.nds "$out" ${./tests/melonds.toml} ${./tests/catalog.json}
      '';
    socketEmulatorCheck =
      pkgs.runCommand "lutin-socket-emulator-check" {
        nativeBuildInputs = emulatorTools;
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/check-sockets-emulator.sh} ${socketTestRom}/lutin.nds "$out" ${./tests/melonds.toml}
      '';
    presentationCheck =
      pkgs.runCommand "lutin-presentation-check" {
        nativeBuildInputs = emulatorTools ++ [pkgs.ffmpeg-full];
        FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
      } ''
        bash ${./scripts/record-presentation.sh} ${presentationRom}/lutin.nds "$out" ${./tests/presentation} ${./tests/melonds.toml} ${./tests/catalog.json}
      '';
    preCommitCheck = inputs.git-hooks.lib.${system}.run {
      package = pkgs.prek;
      src = ./.;
      excludes = [
        "^\\.specify/(scripts|templates|integrations|workflows)/"
        "^\\.opencode/commands/speckit\\."
        "^tests/demos/(2d|3d)/"
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
        ruff.enable = true;
        ruff-format.enable = true;
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
      network-stall-rom = networkStallRom;
      live-agent = liveAgent;
      presentation-rom = presentationRom;
      creation-replay-rom = creationReplayRom;
      presentation = presentationCheck;
    };
    checks.${system} = {
      hardware-emulator =
        pkgs.runCommand "lutin-hardware-check" {
          nativeBuildInputs = emulatorTools;
          FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
        } ''
          bash ${./scripts/check-hardware-emulator.sh} ${hardwareRom}/lutin.nds "$out" ${./tests/melonds.toml}
        '';
      creation =
        pkgs.runCommand "lutin-creation-check" {
          nativeBuildInputs = [pkgs.stdenv.cc];
          buildInputs = [pkgs.zlib];
        } ''
          cc -std=c11 -DLUA_USE_APICHECK -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${jsonSource}/embedded -I${./source} \
            ${./tests/creation.c} ${./source/tools.c} ${./source/image.c} ${./source/runtime.c} ${./source/workspace.c} \
            ${luaSource}/src/*.c ${jsonSource}/embedded/cJSON.c -Wl,--wrap=rename -lz -lm -o test-creation
          timeout 20 ./test-creation ${./examples/playtest-platformer.lua} ${./tests/demos/platformer-test.lua}
          mkdir -p "$out"
          cp work/projects/1/capture-*.png "$out/"
        '';
      catalog = pkgs.runCommand "lutin-catalog-check" {nativeBuildInputs = [pkgs.stdenv.cc pkgs.python3];} ''
        cp ${./tests/catalog.json} models.json
        chmod u+w models.json
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} -I${jsonSource}/embedded \
          ${./tests/catalog.c} ${./tests/fat-rename.c} -Wl,--wrap=rename ${./source/backend.c} ${jsonSource}/embedded/cJSON.c -o test-catalog
        timeout 10 ./test-catalog
        mkdir published
        cp ${./catalog/models.json} published/models.json
        ./test-catalog published
        python3 ${./tests/catalog-generator.py} ${./scripts/build-catalog.py}
        touch "$out"
      '';
      media = pkgs.runCommand "lutin-media-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        mkdir -p projects/1
        cp ${./examples/media.lua} projects/1/main.lua
        cp ${./examples/hero.lua} projects/1/hero.lua
        cp ${./examples/sounds.lua} projects/1/sounds.lua
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I${luaSource}/src -I${./source} -I${jsonSource}/embedded \
          ${./tests/media.c} ${./source/runtime.c} ${./source/workspace.c} ${jsonSource}/embedded/cJSON.c ${luaSource}/src/*.c -lm -o test-media
        timeout 10 ./test-media
        touch "$out"
      '';
      reproducible-rom = reproducibleRomCheck;
      creation-replay-rom = creationReplayRom;
      presentation-rom = presentationRom;
      creation-2d-e2e = creationReplayCheck "2d" ./tests/demos/2d;
      creation-3d-e2e = creationReplayCheck "3d" ./tests/demos/3d;
      creation-freeze-e2e =
        pkgs.runCommand "lutin-freeze-e2e" {
          nativeBuildInputs = emulatorTools ++ [pkgs.ffmpeg-full];
          FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
        } ''
          if LUTIN_TEST_FREEZE=1 bash ${./scripts/record-creation.sh} ${creationReplayRom}/lutin.nds ${./tests/demos/2d} "$out" ${./tests/melonds.toml} ${./catalog/models.json} > freeze.log 2>&1; then
            echo "The E2E check accepted frozen gameplay." >&2
            exit 1
          fi
          grep -q "E2E gameplay did not advance" freeze.log
          cp freeze.log "$out/"
        '';
      creation-inspection-e2e =
        pkgs.runCommand "lutin-inspection-e2e" {
          nativeBuildInputs = emulatorTools ++ [pkgs.ffmpeg-full];
          FONTCONFIG_FILE = pkgs.makeFontsConf {fontDirectories = [pkgs.dejavu_fonts];};
        } ''
          if LUTIN_TEST_INSPECTION=1 bash ${./scripts/record-creation.sh} ${creationReplayRom}/lutin.nds ${./tests/demos/2d} "$out" ${./tests/melonds.toml} ${./catalog/models.json} > inspection.log 2>&1; then
            echo "The E2E check accepted missing inspection state." >&2
            exit 1
          fi
          grep -q "E2E PLAY FAIL inspection unavailable" "$out/emulator.log"
          if grep -q "E2E PLAY PASS" "$out/emulator.log"; then exit 1; fi
          cp inspection.log "$out/"
        '';
      presentation = presentationCheck;
      workflows = pkgs.runCommand "lutin-workflow-check" {nativeBuildInputs = [pkgs.actionlint pkgs.shellcheck];} ''
        actionlint ${./.github/workflows/ci.yml}
        actionlint ${./.github/workflows/catalog.yml}
        touch "$out"
      '';
      input = pkgs.runCommand "lutin-input-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} ${./tests/input.c} -o test-input
        ./test-input
        touch "$out"
      '';
      config = pkgs.runCommand "lutin-config-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
        cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I${./source} -I${jsonSource}/embedded \
          ${./tests/config.c} ${./source/backend.c} ${./source/config.c} ${jsonSource}/embedded/cJSON.c -o test-config
        cp ${./tests/catalog.json} models.json
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
      network-emulator = networkEmulatorCheck;
      socket-emulator = socketEmulatorCheck;
      keyboard-emulator = keyboardEmulatorCheck;
      pre-commit = preCommitCheck;
      runtime = runtimeCheck;
      agent = agentCheck;
      live-agent-build = liveAgent;
      sse = pkgs.runCommand "lutin-sse-check" {nativeBuildInputs = [pkgs.stdenv.cc];} ''
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
          pkgs.python3
          pkgs.git
          pkgs.sops
          pkgs.age
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
