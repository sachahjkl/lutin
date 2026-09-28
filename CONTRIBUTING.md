# Contributing to Lutin

## Development environment

Use Linux x86-64 with Nix and flakes enabled.
The flake supplies BlocksDS, Lua, networking libraries, formatters, and melonDS.

```sh
nix develop
make
prek run --all-files
nix flake check "path:$PWD" --no-write-lock-file
```

The development shell installs hooks through `prek`.
All automated checks run without an API key.
Live inference uses your own OpenCode Go account and consumes its allowance.

## Changes

- Keep the interface, documentation, comments, and identifiers in English.
- Keep native work bounded on embedded hardware.
- Add a regression test for a behavior change or a failure fix.
- Expose new automated checks through `flake.nix`.
- Keep keys, saved conversations, console dumps, and personal paths outside commits.
- Document whether evidence comes from a host test, an emulator, or a physical console.

## Bug reports

Include the ROM revision, console model, launcher, selected model, and exact error message.
Describe the shortest sequence that reproduces the problem.
For input problems, specify the active view, the buttons held, and whether the agent was busy.
Remove credentials and personal conversation text from shared logs.

## Project layout

See the architecture diagram in `README.md` and the contracts in `specs/001-dsi-ai-sandbox/contracts/`.
The development decision record is in `specs/001-dsi-ai-sandbox/`.

Provider definitions and models are separate in `source/backend.h`.
Give each provider a stable ID and each model a `provider/model` ID.
The provider owns its service URL, authentication header, and optional session header.
Its credential is loaded from `/lutin/keys/<provider-id>`.
Add protocol and transport tests when integrating a provider.

## CI and releases

GitHub Actions runs `nix flake check` for pull requests and branch pushes.
Tags matching `vMAJOR.MINOR.PATCH` run the same checks before publishing a release.
The release contains the ROM, a small SD ZIP, and SHA-256 checksums.
Release notes come from `docs/releases/TAG.md` when present.

To build those assets locally:

```sh
nix develop --command bash scripts/package-release.sh dist v0.3.0
```
