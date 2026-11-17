# OpenCode + local Qwen3.8

Project-local configuration: `opencode.json`. Existing AGENTS.md and flight
software are unchanged. Global OpenCode/Ollama settings are not modified.
OpenCode automatically reads AGENTS.md; the configuration additionally loads
the concise OPENCODE_HANDOFF.md. Other scoped instructions are read on demand.

Observed 2026-09-21: Windows OpenCode 1.18.31 and Ollama 0.34.2 are installed;
Ollama responds locally but its model inventory is empty. The installed CLI
accepted `opencode.cmd --pure debug config` and listed `ollama/qwen3.8:latest`
with `opencode.cmd --pure models ollama`; both exited 0. Configuration readiness
does not establish successful inference or tool calling. Per user direction,
preparation is configuration-only: no model download or inference was performed.

## First use (PowerShell in this repository)

Download the model if it is not already present (approximately 18 GB):

```powershell
ollama pull qwen3.8
ollama list
```

Use the exact model name `qwen3.8`, not `qwen 3.8` or `qwen3:8b`.
The project chooses `ollama/qwen3.8:latest` for both main and small-model work;
no cloud provider is enabled by this project configuration.

Configure Ollama to serve at least 65,536 context tokens before sustained use.
The OpenCode context limit is a client budget; it does NOT enlarge Ollama's
server context. If managing the server manually, first quit the existing Ollama
tray/server instance, then use a dedicated PowerShell terminal:

```powershell
$env:OLLAMA_CONTEXT_LENGTH = '65536'
ollama serve
```

This environment setting applies only to that terminal/server, not globally.
Do not start a second server on the occupied port. Adequate RAM/VRAM is required;
hardware capacity and throughput have not been qualified. After the first model
request, use `ollama ps` to inspect the loaded context and CPU/GPU placement.

Start OpenCode from the project root in another terminal:

```powershell
opencode.cmd
```

The `.cmd` shim works with the current Windows PowerShell execution policy;
no policy bypass/change is needed. Request a small read-only first task, e.g.
"Read AGENTS.md and the current gate; summarize the next task without edits."
Do not select auto-approve. Shell commands, edits, delegation and external-path
access require approval. Permissions are approval controls, not an OS sandbox;
do not approve a shell command merely because the assistant describes it as safe.

The endpoint is Windows-local `http://127.0.0.1:11434/v1`. This setup is for
Windows OpenCode with shell-driven WSL builds, not an independently installed
WSL OpenCode client. WSL networking to Windows would need separate verification.
Do not expose Ollama to the LAN to work around a connection problem.

Validation without an inference request:

```powershell
opencode.cmd --pure debug config
opencode.cmd --pure models ollama
```

`--pure` disables external plugins. Model listing is configuration discovery,
not proof that weights are downloaded or that inference works. No new plugin,
package installation or model inference is needed to prepare these files.

References: [Ollama/OpenCode setup](https://docs.ollama.com/integrations/opencode),
[context length](https://docs.ollama.com/context-length),
[OpenCode permissions](https://opencode.ai/docs/permissions/),
[Qwen3.8 model](https://registry.ollama.com/library/qwen3.8).
