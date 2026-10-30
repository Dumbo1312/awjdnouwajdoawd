# ArceusCode

**ArceusCode** is a fork of [OpenCursor](https://github.com/noah-thing/open-cursor)
(which is itself a fork of VS Code / Code-OSS) with yellow-and-black branding
and an **Arceus Presets** extension that gives the agent switchable personas
(General, Coding, Agentic, Review, Brainstorm, Research) and framework commands
(`/fix`, `/feature`, `/review`, `/explain`, `/test`, `/refactor`, `/image`).

This directory is a **fork kit**, not a full source tree. We don't commit
~1.5 GB of upstream source — instead, a build script clones OpenCursor fresh
and overlays our changes.

## Build

### Linux / macOS

```sh
./scripts/fork.sh                 # creates ../arceuscode-src from OpenCursor
cd ../arceuscode-src
npm install                       # ~5 min, pulls Electron
(cd extensions/arceuscode-presets && npm install && npm run build)
npm run compile                   # ~5-15 min
./scripts/code.sh                 # launch ArceusCode
```

### Windows (PowerShell, admin not needed)

```powershell
.\scripts\fork.ps1
cd ..\arceuscode-src
npm install
cd extensions\arceuscode-presets ; npm install ; npm run build ; cd ..\..
npm run compile
.\scripts\code.bat
```

### Prereqs
- **Node 20.x** (OpenCursor's `.nvmrc` is authoritative)
- **Python 3.11** (for `node-gyp` native modules)
- **git**
- **Build tools:**
  - Windows: Visual Studio 2022 "Desktop development with C++" workload
  - macOS: Xcode CLT (`xcode-select --install`)
  - Linux: `build-essential libx11-dev libxkbfile-dev libsecret-1-dev libnss3 fakeroot`

Packaging a signed installer is a separate step — see `build/gulpfile.vscode.js`
in the cloned source for `vscode-win32-x64`, `vscode-darwin-arm64`, etc.

## What's in this kit

```
arceuscode/
├── scripts/
│   ├── fork.sh                 bash: clone + overlay
│   └── fork.ps1                PowerShell: same
├── patches/
│   └── product.json            replaces upstream product.json
└── overlay/
    ├── extensions/
    │   ├── theme-arceuscode/   Arceus Dark + Arceus Light color themes
    │   └── arceuscode-presets/ The presets + framework-commands extension
    └── resources/branding/     Drop icons (.ico, .icns, .png) here
```

## The presets extension

`extensions/arceuscode-presets/` is a normal VS Code extension that ships with
every ArceusCode build. It contributes:

| Command | Shortcut | What it does |
|---|---|---|
| `Arceus: Pick Preset` | `Ctrl+Alt+P` | Switch the agent's system-prompt persona |
| `Arceus: Edit Presets` | — | Opens settings for `arceusCode.presets.library` |
| `Arceus: Open Current Preset` | — | Shows the active system prompt in an editor |
| `Arceus: Run Framework Command` | `Ctrl+Alt+K` | Pick `/fix`, `/feature`, `/review`, etc. — sends a framework-shaped prompt to the chat |
| `Arceus: Edit Framework Commands` | — | Opens settings for `arceusCode.commands.library` |

A status-bar item on the right shows the active preset — click to switch.

### Settings

Both the presets library and the commands library are plain JSON arrays in
`settings.json`:

```jsonc
"arceusCode.presets.active": "coding",
"arceusCode.presets.library": [
	{
		"id": "security",
		"name": "Security Review",
		"icon": "$(shield)",
		"systemPrompt": "You are Arceus-Security. For every change, …",
		"tools": "readOnly",
		"temperature": 0.1
	}
],
"arceusCode.commands.library": [
	{
		"id": "perf",
		"name": "/perf — find a performance bug",
		"prompt": "FRAMEWORK: perf. …",
		"allowedTools": "all",
		"permissionMode": "acceptEdits"
	}
]
```

User entries override built-ins with the same `id` — so you can tweak the
defaults without forking the extension itself.

### How the preset actually reaches the agent

The extension tries to call `openCursor.chat.setSystemPrompt` when you switch
presets. That command's name may change as upstream evolves; the config key
`arceusCode.presets.active` is the source of truth, and the chat panel can
read it directly as a fallback. If you add an agent of your own, read that key.

### Framework commands

Each framework command is a prompt scaffold. When you pick one, the extension:

1. Grabs your selection (if any).
2. Pops an input box for a short note.
3. Builds a single chat message `<FRAMEWORK>\n\nUser note: <note>\n<selection>`
4. Starts a new chat and sends it. If driving the chat panel fails, it
   falls back to copying the composed prompt to your clipboard.

## Branding

To finish the rebrand so it doesn't still read "Code - OSS" or "OpenCursor"
in a few corners:

1. Drop a 256×256 `.ico`, a `.icns`, and `.png` logo set into
   `overlay/resources/branding/` and re-run `fork.sh`.
2. Edit `patches/product.json` if you want to change `nameLong`, `applicationName`,
   bundle IDs, etc.
3. Open the generated `arceuscode-src/resources/` tree and replace the
   remaining icons there — upstream has about 20 image files that are not
   covered by `product.json`. See `arceuscode-src/resources/README.md` for
   the full list.

## Why not fork upstream directly?

- Upstream is ~16,750 files, ~1.5 GB on disk after `npm install`.
- Committing it to your own repo buries real changes in a sea of upstream
  code and leaves you to merge upstream manually forever.
- The overlay approach keeps the diff small and makes `git log` honest.

When OpenCursor ships a new release, pin a tag:

```sh
ARCEUSCODE_UPSTREAM_REF=v0.2.0 ./scripts/fork.sh ../arceuscode-src-v0.2.0
```
