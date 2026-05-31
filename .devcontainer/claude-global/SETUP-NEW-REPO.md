# Setting up Claude Code context for a new repo/container

This documents the shared-context pattern used across openobsw, opensvf, etc.

---

## How it works

```
WSL2 host: ~/.claude-global/          (persists across all container rebuilds)
    CLAUDE.md                          → global style/behaviour rules
    SETUP-NEW-REPO.md                  → this file
    contexts/
        openobsw-opensvf.md            → shared integration context
        <other-repo>.md                → add one per project pair as needed
```

Each project's `CLAUDE.md` imports the relevant context file via:
```
@/home/vscode/.claude-global/contexts/<context-file>.md
```

Claude Code expands `@file` references at load time, so the shared content is
always available without duplicating it in each repo.

---

## Steps for a new repo (e.g. opensvf)

### 1. devcontainer.json — add the mount

In `.devcontainer/devcontainer.json`, add to the `mounts` array:

```json
"source=${localEnv:HOME}/.claude-global,target=/home/vscode/.claude-global,type=bind,consistency=cached"
```

### 2. post-create.sh — wire the symlink

Add at the end of `.devcontainer/post-create.sh` (before the final echo):

```bash
# ── Global CLAUDE.md ──────────────────────────────────────────────────
touch /home/vscode/.claude-global/CLAUDE.md 2>/dev/null || true
ln -sf /home/vscode/.claude-global/CLAUDE.md /home/vscode/.claude/CLAUDE.md
echo "[+] Global CLAUDE.md linked from WSL2 host"
```

### 3. CLAUDE.md — import shared context

At the top of the repo's `CLAUDE.md`, add:

```markdown
@/home/vscode/.claude-global/contexts/openobsw-opensvf.md
```

(Or create a new context file in `~/.claude-global/contexts/` if the repo
has different cross-cutting concerns.)

### 4. Rebuild the container

In VS Code: Ctrl+Shift+P → "Dev Containers: Rebuild Container"

---

## Adding a new shared context file

On the WSL2 host:

```bash
nano ~/.claude-global/contexts/<new-context>.md
# write the shared context, save
```

Then reference it from any repo's `CLAUDE.md`:
```markdown
@/home/vscode/.claude-global/contexts/<new-context>.md
```

No container rebuild needed — the file is live immediately.

---

## Editing the global style rules

```bash
nano ~/.claude-global/CLAUDE.md   # on WSL2 host
```

Changes take effect the next time Claude Code starts a session.
