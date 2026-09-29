# Model catalog

This feature is available from v0.5.0.

Lutin reads `/lutin/models.json` from SD. The ROM contains no provider or model list.
The SD kit supplies the compact catalog built from models.dev and verified transport routes.
Install this file alongside the ROM when upgrading from v0.4.0.

## Commands

Stop the agent before changing the catalog.
Open the START command menu.

- **Reload model catalog** reloads the SD files.
- **Update model catalog** downloads the published compact catalog over verified HTTPS, validates it, saves it, and reloads it.
- B cancels a download. Downloads do not require or send a provider API key.

The update source is `https://raw.githubusercontent.com/sachahjkl/lutin/main/catalog/models.json`.
The console never downloads the full models.dev database.
A failed download or invalid catalog leaves the loaded catalog intact.
The replacement uses a temporary file and backup for FAT recovery.
Model selection is paginated. Saved sessions retain their provider-qualified model IDs.
If a saved model disappears, select an available model before resuming its queue.

## Personal entries

Edit `/lutin/models.local.json` to add or override providers and models.
Updates replace `models.json` and preserve `models.local.json`.
Matching IDs replace complete entries. New IDs append entries.
Omitted arrays leave their corresponding base entries unchanged.
The optional `default_model` overrides the catalog default.
The `default_model` in `config.json` takes precedence over that default.

```json
{
  "providers": [
    {
      "id": "my-provider",
      "base_url": "https://api.example.com/v1",
      "auth_header": "Authorization: Bearer"
    }
  ],
  "models": [
    {
      "id": "my-provider/my-model",
      "name": "My model",
      "provider": "my-provider",
      "model": "my-model",
      "protocol": "chat-completions"
    }
  ]
}
```

Replace the example URL and model identifier with your provider's values.
Put its key in `/lutin/keys/my-provider`.
`auth_header` contains the header prefix, never the key.
For example, `Authorization: Bearer` produces `Authorization: Bearer <key>`.
An optional `session_header` names the header that carries the session identifier.
Supported protocols are `responses` and `chat-completions`.
The optional per-model `reasoning_effort` sets the provider's reasoning level. Omit it when unsupported.
Set the per-model boolean `image_input` to `true` when the selected route supports image input.
If this field is absent or false, the capture tool reports that image input is unsupported.
The catalog generator reads this capability from models.dev input modalities.
The transport appends `/responses` or `/chat/completions` to the provider base URL.
Adding a provider that needs another protocol still requires implementing that protocol.

## Base format and limits

The base document contains `version: 1`, `default_model`, `providers`, and `models`.
See [`catalog/models.json`](../catalog/models.json) for the complete file.
Manual base edits work after reloading, but the next download replaces them.
Use the local file for changes that must survive updates.

Each file must contain fewer than 65,536 bytes.
The merged catalog supports up to 16 providers and 128 models.
These are memory bounds, not predefined entries.
Unknown fields, duplicate keys or IDs, missing providers, invalid protocols, and non-HTTPS URLs are rejected.
An invalid local file rejects the whole reload or update.
If no valid catalog has loaded, local programs remain usable but inference requires a valid catalog.

## Build and publish

`catalog/routes.json` defines supported provider connections and verified per-model protocols.
`scripts/build-catalog.py` combines these routes with model names and text/tool capabilities from models.dev.
Models without a verified route are excluded. Metadata alone does not establish protocol compatibility.

```sh
curl --fail --location --user-agent 'Lutin catalog builder' https://models.dev/api.json -o /tmp/opencode/models-dev.json
nix develop --command python3 scripts/build-catalog.py /tmp/opencode/models-dev.json catalog/routes.json catalog/models.json
```

The **Update model catalog** GitHub workflow runs daily and supports manual dispatch.
It validates the generated catalog before publishing changed data to `main`.
To add a supported model, add its route and regenerate the catalog. Recompiling the ROM is unnecessary.
