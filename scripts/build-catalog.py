#!/usr/bin/env python3
"""Build the compact SD catalog from models.dev and verified transport routes."""

import argparse
import json
from pathlib import Path


def build(source, routes):
    catalog = {key: routes[key] for key in ("version", "default_model", "providers")}
    catalog["models"] = []
    for route in routes["models"]:
        provider = source[route["provider"]]
        model = provider["models"][route["model"]]
        if not model.get("tool_call") or "text" not in model["modalities"]["output"]:
            raise ValueError(f"Model lacks text tools: {route['model']}")
        catalog["models"].append(
            {
                "id": f"{route['provider']}/{route['model']}",
                "name": f"{provider['name']} / {model['name']}",
                **route,
                "image_input": "image" in model["modalities"].get("input", []),
            }
        )
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("routes", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    catalog = build(
        json.loads(args.source.read_text()), json.loads(args.routes.read_text())
    )
    args.output.write_text(json.dumps(catalog, indent=2) + "\n")


if __name__ == "__main__":
    main()
