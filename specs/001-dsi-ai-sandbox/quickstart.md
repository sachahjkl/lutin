# Verification quickstart

## Build and check

```sh
nix develop
make
prek run --all-files
nix flake check "path:$PWD" --no-write-lock-file
```

The build produces `lutin.nds`.
Automated checks do not use API credentials.

## Console

1. Follow `docs/install-dsi.md` to prepare the SD card.
2. Launch the ROM in DSi mode.
3. Open START → Run program for the offline example.
4. Open Play controls for an interactive creation.
5. Press START to return to the menu.

## Authenticated host inference

```sh
nix build .#live-agent -o result-live-agent
result-live-agent/bin/live-agent /path/to/private-test-directory opencode-go/deepseek-v4-flash 'Create and run an animation.'
```

Place `keys/opencode-go` and `ca.pem` in the private test directory first.
This test consumes your service allowance.
Keep the directory outside the repository and Nix store.
Check for a completed agent turn, a running creation, and no runtime error.
