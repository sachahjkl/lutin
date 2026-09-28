# Tools and Code Mode

## Execution model

Explicit JSON tools support ordinary file and runtime operations.
Code Mode runs short Lua scripts with the same operations exposed through `tools`.
Each call records its ID, name, arguments, and result in the session journal.
Tools run sequentially after a complete generation arrives.

| Tool              | Purpose                                       |
| ----------------- | --------------------------------------------- |
| `list_files`      | List project filenames in pages.              |
| `read_file`       | Read a bounded page with a full-file version. |
| `write_file`      | Replace or append with an expected version.   |
| `patch_file`      | Replace exactly one matching text block.      |
| `run_program`     | Load and start a complete Lua file.           |
| `inspect_runtime` | Read state, memory, and last error.           |
| `read_logs`       | Read diagnostics from a cursor.               |
| `execute`         | Run a bounded Lua Code Mode script.           |

Code Mode additionally exposes `stop_program` and `capture_screen`.
All paths are flat filenames relative to the active project.
Credentials and session journals are not writable through these tools.
Captures return local references, not model image inputs.

## File versions and size limits

An absent file has version `missing`.
Existing file versions are opaque eight-digit hexadecimal hashes.
Writes and patches return the next version for the following operation.
Stale versions are rejected rather than overwritten.

JSON reads return pages of at most 4,096 bytes.
JSON writes and replacement blocks accept at most 8,192 bytes each.
The complete project text file remains limited to 32 KiB.
Run the program only after the final write chunk.
The previous loaded creation keeps running during edits.

## Recovery

The host persists an interrupted-call marker before executing a tool.
A repeated completed ID returns its saved result.
An interrupted ID requires inspection rather than automatic replay.
Storage replacement retains a backup until the new file is installed.

Output-limit recovery requests a smaller generation at most twice per turn.
Incomplete generations execute no tools.
The Chat Completions adapter passes all schemas and assembles up to four calls in order.
Network loss does not replay a request.

The authenticated host platformer test validated incremental creation with DeepSeek V4 Flash.
Physical FAT power-loss recovery remains a separate hardware test.
