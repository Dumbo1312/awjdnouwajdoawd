# Arceus

Two projects live here, both aimed at "my own personal coding/image AI":

| Dir | What it is | Build | Status |
|---|---|---|---|
| **`arceuscode/`** | **Fork kit for ArceusCode** — a yellow-and-black fork of [OpenCursor](https://github.com/noah-thing/open-cursor) (itself a fork of VS Code) with a Presets extension that gives the agent switchable personas and `/fix` `/feature` `/review` framework commands. | `./arceuscode/scripts/fork.sh` → clones upstream, overlays our changes → `npm install && npm run compile` in the resulting dir. ~15 min first build. | Primary focus. |
| **`arceus-qt/`** | **Arceus AI** — standalone native Qt 6 / C++ chat app. 22 provider integrations (OpenAI, Anthropic, Gemini, Groq, OpenRouter, 15+ more, plus local Ollama/LM Studio), agentic tool loop with permission modes, 6 presets. Not an IDE, just a chat window. | Qt Creator opens `arceus-qt/CMakeLists.txt` → Build. ~1 min. | Done, built once in an earlier turn. Kept around in case you want a tiny native fallback. |

Start with **`arceuscode/`** — see `arceuscode/README.md` for the full build
flow. The Qt app is a side bet.

## Project layout

```
.
├── arceuscode/                  ← fork kit (small; produces a real IDE)
│   ├── scripts/fork.sh          clone OpenCursor + overlay
│   ├── scripts/fork.ps1         same, Windows
│   ├── patches/product.json     ArceusCode branding
│   └── overlay/
│       ├── extensions/
│       │   ├── theme-arceuscode/        yellow/black color themes
│       │   └── arceuscode-presets/      Presets + framework commands
│       └── resources/branding/  drop icons here
│
└── arceus-qt/                   ← native desktop fallback (fully built)
    ├── CMakeLists.txt
    ├── src/
    └── resources/
```

## Next steps

- Install the Claude GitHub App on this repo so I can push:
  https://github.com/apps/claude/installations/select_target
- Then say "retry push" and the fork kit lands on `claude/chat-session-9on4ll`.
- Once you build ArceusCode once locally, we can start adding commands
  (`/sec-review`, `/port`, whatever) by editing `arceuscode-presets/src/defaults.ts`
  — each command is its own small framework the agent follows.
