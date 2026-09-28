# Data model

## Local configuration

`/ai-dsi/config.json` contains optional `default_model`, `tool_details`, and `startup_view` fields.
The API key lives separately in `/ai-dsi/opencode-key`.
The CA bundle lives in `/ai-dsi/ca.pem`.
These files are outside the tools' project namespace.

## Project

A project is a numbered directory under `/ai-dsi/projects/`.
Its flat files include the Lua entry point `main.lua` and session documents.
Saved creations can run offline.
Writes use an internal temporary file and backup to recover failed replacement.

## Session

Each `session-N.json` is a single JSON document, capped at 192 KiB.
It contains:

- `history`: ordered conversation items and tool results.
- `queue`: pending request strings.
- `journal`: tool-call records indexed by call ID.
- `model`: the selected service model ID as a string.
- `network_id`: the persistent OpenCode session identifier.
- `interruptions`: incomplete responses when present.

Session discovery reads the directory in bounded pages.
One session controls the agent at a time.
Opening a session does not automatically resume its Queue.
Reset clears history, Queue, and journal, retains the model, and creates a new network identity.
Delete removes the session file and its backup, not project creations.
Session titles and context summaries are future work, not current fields.

## Tool calls

Each journal record retains the tool name, arguments, result, and success state when available.
Before execution, the host persists an interrupted-call marker.
After execution, it saves the actual result.
A repeated ID returns the recorded result rather than repeating the operation.
An interrupted marker requires inspection, not automatic replay.

File versions are opaque eight-digit hexadecimal content hashes.
An absent file uses the literal version `missing`.
Each successful write returns the next version.

## Connection states

```text
idle → Wi-Fi association → DNS → TCP → TLS → waiting → streaming → idle
```

Errors retain an actionable status. Cancellation closes the current request.
Wi-Fi reconnection does not replay a request.

## Runtime states

```text
stopped → loading → running → stopped
             ↓         ↓
           error     error
```

A syntax failure preserves the previously loaded program.
A callback failure stops the affected creation and retains its diagnostic.
