import runpy
import sys

build = runpy.run_path(sys.argv[1])["build"]
source = {
    "example": {
        "name": "Example",
        "models": {
            "text": {
                "name": "Text",
                "tool_call": True,
                "modalities": {"output": ["text"]},
            },
            "image": {
                "name": "Image",
                "tool_call": False,
                "modalities": {"output": ["image"]},
            },
        },
    }
}
routes = {
    "version": 1,
    "default_model": "example/text",
    "providers": [{"id": "example"}],
    "models": [{"provider": "example", "model": "text", "protocol": "responses"}],
}
catalog = build(source, routes)
assert len(catalog["models"]) == 1
assert catalog["models"][0]["id"] == "example/text"
assert catalog["models"][0]["name"] == "Example / Text"
assert catalog["models"][0]["protocol"] == "responses"
routes["models"][0]["model"] = "image"
try:
    build(source, routes)
except ValueError:
    pass
else:
    raise AssertionError("Unsupported image model accepted")
print("Catalog generator: verified routes, names and text/tool filtering passed")
