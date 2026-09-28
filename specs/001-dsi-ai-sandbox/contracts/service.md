# OpenCode Go service contract

Lutin calls OpenCode Go directly over HTTPS.
The personal API key is loaded from `/ai-dsi/opencode-key` on SD.

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

The client validates certificates with `/ai-dsi/ca.pem` and does not follow HTTP redirects.
Available models depend on OpenCode Go and the user's account.
The project supplies no account or API key.

Source: <https://opencode.ai/v2/docs/console/go/>.
