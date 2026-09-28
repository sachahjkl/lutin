# Third-party software

The MIT license in `LICENSE` applies to original Lutin code and documentation.
Dependencies and installation tools retain their own licenses and notices.

| Component                                     | Purpose                                                 | Upstream                                      |
| --------------------------------------------- | ------------------------------------------------------- | --------------------------------------------- |
| BlocksDS and libnds                           | Console toolchain, hardware access, cooperative threads | <https://blocksds.skylyrac.net/>              |
| DSWiFi                                        | Console Wi-Fi                                           | <https://github.com/blocksds/dswifi>          |
| Lua 5.4.9                                     | Program runtime and tool scripting                      | <https://www.lua.org/license.html>            |
| cJSON                                         | JSON parsing                                            | <https://github.com/DaveGamble/cJSON>         |
| libcurl                                       | HTTP transport                                          | <https://curl.se/docs/copyright.html>         |
| Mbed TLS                                      | TLS and certificate validation                          | <https://github.com/Mbed-TLS/mbedtls>         |
| zlib                                          | Compression dependency                                  | <https://zlib.net/zlib_license.html>          |
| TWiLight Menu++ and nds-bootstrap             | Console launcher                                        | <https://github.com/DS-Homebrew/TWiLightMenu> |
| Safe Unlaunch installer, dumpTool, Memory Pit | Optional console setup tools                            | <https://dsi.cfw.guide/>                      |
| melonDS                                       | Emulator checks and presentation capture                | <https://melonds.kuribo64.net/>               |
| Spec Kit                                      | Development templates and scripts                       | <https://github.com/github/spec-kit>          |

`flake.lock`, `nix/network-libraries.nix`, and `nix/installation-sources.json` pin dependency sources.
The installation kit includes separate upstream tools. It does not relicense those tools under MIT.
Lutin is an independent homebrew project. It is not affiliated with Nintendo or OpenCode.
