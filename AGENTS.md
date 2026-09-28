# Lutin

## Project scope

- Write project documentation, code comments, the interface, and program messages in English.
- Use English identifiers in code.
- Read `.specify/memory/constitution.md` before architecture changes.
- Read `specs/001-dsi-ai-sandbox/plan.md` for design decisions and remaining work.
- Use `SPECIFY_FEATURE=001-dsi-ai-sandbox` for Spec Kit scripts on `master`.

## Development

- Use `nix develop` to load tools.
- Build with `make` or `nix build .#rom`.
- Run `prek run --all-files` after changes.
- Run `nix flake check "path:$PWD" --no-write-lock-file` before delivery.
- Expose new checks through `checks`.
- Exclude upstream Spec Kit files from automatic formatting.
- Use backlog tools when available.
- If backlog tools are unavailable, report the blocker without editing `BACKLOG.json`.

## Integration

- Read `specs/001-dsi-ai-sandbox/contracts/service.md` for the OpenCode Go contract.
- Send the OpenCode Go key in `Authorization: Bearer …`.
- Load secrets at runtime.
- Keep secrets outside sources, ROMs, and the Nix store.
- Verify networking on a physical DSi before claiming hardware support.
- Keep input interrupt handlers bounded and free of allocation, storage, rendering, and network calls.
