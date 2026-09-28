# Lutin constitution

**Version:** 0.3.0, 2026-09-28.
**Status:** product direction confirmed. Routine technical decisions are delegated to the development agent.

## I. Run creations on the DSi

Remote AI helps create and modify programs.
The console executes them and owns the screens, buttons, and touch input.
Each milestone must produce an observable result on the target or a clearly identified test environment.

## II. Grow from working versions

Keep each delivered version usable and verifiable.
Start with boot, networking, dialogue, local execution, and assisted creation.
Use console measurements to set memory and timing budgets.

## III. Bound the sandbox

Expose a documented API to creations.
Limit memory, Lua instructions, and drawing work.
An infinite Lua loop must not require restarting the console.
Use separate Lua states for persistent creations and short Code Mode scripts.
Treat hardware timing limits as targets until measured.

## IV. Reuse maintained components

Use BlocksDS for the native toolchain and console libraries.
Use OpenCode Go directly over HTTPS without personal infrastructure.
Keep networking, interface, storage, and execution separate.
Add dependencies only for a required capability.

## V. Make results verifiable

Pin development tools with Nix. Expose automated checks through `checks`.
Use `prek` for hooks.
Distinguish host tests, emulator checks, and physical-console evidence.
An HTTPS connection alone does not prove authenticated inference.

## Integration constraints

- Target a homebrew `.nds` application running in DSi mode.
- Verify HTTPS server certificates and hostnames.
- Load the API key at runtime.
- Keep secrets outside sources, ROMs, and the Nix store.
- Capture input independently of expensive main-loop work.
- Keep interrupt handlers bounded and free of allocation or I/O.
- Write the interface and project documentation in English.

## Workflow

1. Update the specification before adding a capability.
2. Document assumptions and verification criteria.
3. Run applicable checks.
4. Record verification limits.

Update this constitution's version when changing a governing principle.
