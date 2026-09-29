#!/usr/bin/env python3
"""Prepare credential-free replay fixtures from a completed live session."""

import argparse
import json
import shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("recording", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
session = json.loads((args.recording / "projects/1/session-1.json").read_text())
responses = json.loads((args.recording / "responses.json").read_text())
log = (args.recording / "run.log").read_text()
if "Agent done" not in log or "Runtime: running" not in log:
    raise SystemExit("The live session did not finish with a running creation.")
expected = []
for response in responses:
    for item in response:
        if item.get("type") == "function_call":
            result = session["journal"][item["call_id"]]["ok"]
            if not isinstance(result, bool):
                raise SystemExit("Missing tool outcome.")
            expected.append(result)
if not expected:
    raise SystemExit("The recording contains no tool calls.")
args.output.mkdir(parents=True, exist_ok=True)
initial = {
    "history": [],
    "queue": [(args.recording / "prompt.txt").read_text().strip()],
    "journal": {},
    "model": session["model"],
}
if (args.recording / "initial-project").is_dir():
    source = args.recording / "initial-project"
    initial = json.loads((source / "session-1.json").read_text())
    initial["queue"] = [(args.recording / "prompt.txt").read_text().strip()]
    shutil.copytree(
        source,
        args.output / "project",
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("session-*.json"),
    )
for name, value in (
    ("session", initial),
    ("responses", responses),
    ("expected", expected),
):
    (args.output / f"{name}.json").write_text(json.dumps(value, indent=2) + "\n")
print(f"Prepared {len(responses)} responses and {len(expected)} tool outcomes.")
