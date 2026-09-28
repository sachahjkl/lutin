# OpenCode Go service contract

Lutin calls OpenCode Go directly over HTTPS.
The provider API key is loaded from `/lutin/keys/opencode-go` on SD.

Provider definitions own their base URL, authentication header, and optional session header.
Model definitions reference a provider and select a protocol.
Configuration and sessions use qualified IDs such as `opencode-go/deepseek-v4-flash`.
The outgoing request sends the provider's model name, such as `deepseek-v4-flash`.
Credential filenames use the provider ID, so separate providers never share an implicit global key.

| Model               | Protocol         | URL                                              |
| ------------------- | ---------------- | ------------------------------------------------ |
| `gpt-6-luna`        | Responses        | `https://opencode.ai/zen/go/v1/responses`        |
| `gpt-5.6-luna`      | Responses        | `https://opencode.ai/zen/go/v1/responses`        |
| `grok-4.6`          | Responses        | `https://opencode.ai/zen/go/v1/responses`        |
| `deepseek-v4-flash` | Chat Completions | `https://opencode.ai/zen/go/v1/chat/completions` |

Requests send `Authorization: Bearer …`, `User-Agent: Lutin/0.3`, and `x-opencode-session`.
The session identifier persists on SD.
Responses use function tools and Server-Sent Events, abbreviated SSE.
The adapter retains DeepSeek reasoning content needed for subsequent tool turns.
Incomplete generations never execute tools.

The client validates certificates with `/lutin/ca.pem` and does not follow HTTP redirects.
Available models depend on OpenCode Go and the user's account.
The project supplies no account or API key.

Source: <https://opencode.ai/v2/docs/console/go/>.
