# Recorded creation tests

The `2d` and `3d` fixtures come from authenticated GPT 6 Luna sessions on 2026-09-29.
The model generated the project source, executed tools, received PNG image input, and ran seeded assertions.

Each fixture contains:

- `project/`: the source and captures before the recorded continuation.
- `session.json`: prior history, tool journal, and the queued continuation prompt.
- `responses.json`: the model's recorded responses for that continuation.
- `expected.json`: the success or failure of each tool call during the live session.

The replay ROM uses the actual agent, tools, and runtime with a recorded transport.
The interface displays `REPLAY`. The production ROM uses HTTPS.
The observer requires matching tool outcomes, a completed agent turn, and a running creation with testing disabled.
It then checks state changes caused by emulated RIGHT and A input.
The freeze test verifies that the gameplay assertion rejects a frozen main loop.

Run the checks with:

```sh
nix build .#checks.x86_64-linux.creation-2d-e2e -o result-creation-2d
nix build .#checks.x86_64-linux.creation-3d-e2e -o result-creation-3d
nix build .#checks.x86_64-linux.creation-freeze-e2e
```

The check outputs contain MP4 recordings, screenshots, and emulator logs.
Recorded fixture files are excluded from formatting to retain their original bytes and file versions.
