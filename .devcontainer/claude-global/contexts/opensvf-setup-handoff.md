# Claude Code Setup Handoff — from openobsw session (May 2026)

This document captures the full context of the Claude Code shared-context
infrastructure built in the openobsw repo, so the same setup can be replicated
in opensvf (and any future repo) with minimal effort.

---

## What problem we solved

Inside a VS Code dev container, `~/.claude/` is ephemeral — wiped on every
container rebuild. That means:
- A global `~/.claude/CLAUDE.md` (user-wide instructions) disappears on rebuild
- Cross-repo context documents have no persistent home
- Every new container starts with zero Claude Code memory

---

## The solution: `~/.claude-global/` bind mount

We bind-mount a directory from the WSL2 host into every container at a fixed
path. That directory persists forever on the WSL2 host, survives all rebuilds,
and is available under the same absolute path in every container.

```
WSL2 host: ~/.claude-global/              ← lives here permanently
    CLAUDE.md                              ← global style/behaviour rules
    SETUP-NEW-REPO.md                      ← setup guide for new repos
    contexts/
        openobsw-opensvf.md               ← shared integration context
        opensvf-setup-handoff.md          ← this file
```

Inside every container: `/home/vscode/.claude-global/` (bind mount, read/write)

A symlink wired up by `post-create.sh` makes Claude Code read the host file:
```
/home/vscode/.claude/CLAUDE.md  →  /home/vscode/.claude-global/CLAUDE.md
```

---

## What was changed in openobsw

### `.devcontainer/devcontainer.json`

Added one entry to the `mounts` array:

```json
"source=${localEnv:HOME}/.claude-global,target=/home/vscode/.claude-global,type=bind,consistency=cached"
```

`${localEnv:HOME}` resolves to the WSL2 user's home at container startup.

### `.devcontainer/post-create.sh`

Added at the end (before the final echo):

```bash
# ── Global claude-global setup ────────────────────────────────────────
CLAUDE_GLOBAL=/home/vscode/.claude-global
mkdir -p "$CLAUDE_GLOBAL/contexts"
# Copy staged files to WSL2 host on first use (never overwrite user edits)
for f in "$REPO/.devcontainer/claude-global/SETUP-NEW-REPO.md" \
          "$REPO/.devcontainer/claude-global/contexts/openobsw-opensvf.md"; do
    dest="$CLAUDE_GLOBAL/${f#*claude-global/}"
    [ -f "$dest" ] || cp "$f" "$dest"
done
touch "$CLAUDE_GLOBAL/CLAUDE.md"
ln -sf "$CLAUDE_GLOBAL/CLAUDE.md" /home/vscode/.claude/CLAUDE.md
echo "[+] Global CLAUDE.md linked; context files seeded to WSL2 host"
```

### `CLAUDE.md`

Added at the very top:

```markdown
@/home/vscode/.claude-global/contexts/openobsw-opensvf.md
```

Claude Code expands `@/path/to/file.md` at load time, injecting the file's
content as additional context. This is how we share context across repos without
duplicating it.

### `.devcontainer/claude-global/` (new directory, committed to repo)

Staged copies of the context files so they survive a container rebuild and
are auto-deployed to the WSL2 host by `post-create.sh`:

```
.devcontainer/claude-global/
    SETUP-NEW-REPO.md
    contexts/
        openobsw-opensvf.md
        opensvf-setup-handoff.md   ← this file
```

On first rebuild, `post-create.sh` copies these to `~/.claude-global/` on the
WSL2 host (skips if file already exists, so user edits are preserved).

---

## Steps to replicate in opensvf

### 1. devcontainer.json — add the mount

```json
"mounts": [
    ...,
    "source=${localEnv:HOME}/.claude-global,target=/home/vscode/.claude-global,type=bind,consistency=cached"
],
```

### 2. post-create.sh — wire the symlink and seed files

Paste the bash block above (substituting the correct staged file paths for
opensvf's repo layout).

### 3. CLAUDE.md — import shared context

At the top of opensvf's `CLAUDE.md`:

```markdown
@/home/vscode/.claude-global/contexts/openobsw-opensvf.md
```

This gives Claude in opensvf full awareness of the openobsw wire protocol,
PUS-C services, SRDB layout, AOCS modes, and integration transport options —
without duplicating the content.

### 4. Rebuild the container

Ctrl+Shift+P → "Dev Containers: Rebuild Container"

After rebuild, `~/.claude-global/` on the WSL2 host is populated and the
symlink is live. All future rebuilds are idempotent.

---

## Editing shared context

The context file `openobsw-opensvf.md` lives on the WSL2 host at
`~/.claude-global/contexts/openobsw-opensvf.md`. Edit it there; both repos
see the update immediately (no rebuild needed, takes effect on next Claude
Code session start).

If opensvf has its own cross-cutting context to share back, add a new file:
```bash
~/.claude-global/contexts/opensvf-specific.md
```
Then import it from any repo that needs it.

---

## Global CLAUDE.md (style / behaviour rules)

`~/.claude-global/CLAUDE.md` is the global instruction file. Edit it on the
WSL2 host to apply rules to every repo:

```bash
nano ~/.claude-global/CLAUDE.md
```

Typical contents: response style rules, git behaviour, code conventions that
apply across all projects.
