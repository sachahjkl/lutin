# Installing Lutin on a DSi

Lutin targets the Nintendo DSi and DSi XL, launched from the console SD slot in DSi mode.
The tested setup uses Unlaunch and TWiLight Menu++.
The interface and messages are in English.

## A console that already runs homebrew

Build the kit:

```sh
nix build .#installation-kit -o result-installation
```

1. Copy `02-menu/roms/nds/ai-dsi.nds` to `/roms/nds/ai-dsi.nds` on the SD card.
2. Copy `02-menu/ai-dsi/ca.pem` to `/ai-dsi/ca.pem`.
3. Copy `02-menu/ai-dsi/config.json` to `/ai-dsi/config.json` for an initial installation.
4. Save your OpenCode Go API key in `/ai-dsi/opencode-key` as plain text.
5. Configure Wi-Fi and the correct date and time in the console system settings.
6. Launch the ROM through TWiLight Menu++ in DSi mode.

For an offline example, copy `02-menu/ai-dsi/projects/1/main.lua` into the corresponding SD directory.
Do not replace an existing creation with this example.
For later updates, replace the ROM and update the CA bundle when needed.

See [configuration and controls](usage.md) for API keys, models, sessions, and the Lua API.

## A console without a homebrew launcher

Follow **[DSi CFW Guide](https://dsi.cfw.guide/)** for the current installation procedure.
It covers SD preparation, an initial entry point, NAND backup, Unlaunch, and TWiLight Menu++.
Installing Unlaunch writes to internal storage. Complete the guide's NAND backup before that step.

The optional Lutin kit contains pinned downloads used by that procedure:

| Directory                 | Contents                                                |
| ------------------------- | ------------------------------------------------------- |
| `01-backup/`              | dumpTool named `boot.nds`.                              |
| `memory-pit/facebook/`    | Memory Pit for the camera album with the Facebook icon. |
| `memory-pit/no-facebook/` | Memory Pit for the album without that icon.             |
| `02-menu/`                | TWiLight Menu++, Safe Unlaunch installer, and Lutin.    |
| `emulation/`              | dsibiosdumper for your own BIOS and firmware copies.    |
| `downloads/`              | Original archives, source URLs, and SHA-256 checksums.  |

Copy one installation stage at a time.
The dumpTool `boot.nds` and TWiLight Menu++ `BOOT.NDS` names collide on FAT.
Use the official guide to choose the correct Memory Pit variant and SD format.
The kit does not include BIOS, firmware, NAND images, or API credentials.

### SD card preparation

Use the console SD slot. A microSD card with an SD adapter works with the same layout.
Keep a backup of existing SD contents before formatting.
Use FAT32 according to the [official SD setup instructions](https://dsi.cfw.guide/sd-card-setup.html).
The kit includes the upstream Linux formatter archive.
`nix develop` provides `sdFormatLinux`, `mkfs.fat`, `f3write`, and `f3read`.
The Nix wrapper adapts the formatter for NixOS.

### Boot setup

The official guide explains how to assign TWiLight Menu++ to Unlaunch's `NO BUTTON` action.
Use the launcher from `sdmc:/BOOT.NDS`.
Keep a route back to the Nintendo launcher, such as the B-button action.
Holding A + B at startup opens Unlaunch on the tested setup.

## First launch

1. Open `/roms/nds/ai-dsi.nds` from TWiLight Menu++.
2. Check that the upper screen reports DSi mode.
3. Press START and select Run program.
4. Open Play controls to route buttons and touch to an interactive creation.
5. Press START to return to the menu.

Run program starts the project file or the built-in animation when no entry file is available.
Stop program ends it. Quit exits the application after saving.
If the mode is DS, check the launch location and DSi-mode settings before testing networking.

## Troubleshooting

| Symptom                            | Check                                                                      |
| ---------------------------------- | -------------------------------------------------------------------------- |
| Missing key                        | The file must be `/ai-dsi/opencode-key`, containing only the key.          |
| Certificate error                  | Check the console clock and `/ai-dsi/ca.pem`.                              |
| Wi-Fi association fails            | Check the console Wi-Fi profile, DSi mode, and access-point compatibility. |
| HTTP 401 or 403                    | Check the key and account access through OpenCode Go.                      |
| Buttons do not affect the creation | Select Play controls, then release buttons held while closing the menu.    |
| Config diagnostic                  | Check JSON syntax, supported keys, and value types.                        |
| Save failure                       | Check SD free space and filesystem health.                                 |

Input capture cannot guarantee immediate feedback during every TLS or storage operation.
Report the active view and agent state with input problems.

## Emulation

The automated melonDS check uses DS-mode replacement BIOS support.
It verifies the interface and runtime without private console files.
Full DSi emulation requires BIOS, firmware, and a NAND copy from your console.

1. Run the supplied dsibiosdumper according to its upstream instructions.
2. Configure those files in melonDS DSi settings.
3. Use a working copy of your NAND image.

Keep console dumps outside the repository and Nix store.
The DS-mode automated check does not validate the DSi Wi-Fi stack.

## References

- [DSi CFW Guide](https://dsi.cfw.guide/)
- [SD card setup](https://dsi.cfw.guide/sd-card-setup.html)
- [Memory Pit](https://dsi.cfw.guide/launching-the-exploit.html)
- [NAND backup](https://dsi.cfw.guide/dumping-nand.html)
- [Unlaunch installation](https://dsi.cfw.guide/installing-unlaunch.html)
- [TWiLight Menu++](https://wiki.ds-homebrew.com/twilightmenu/installing-dsi)
