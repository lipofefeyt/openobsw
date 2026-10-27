#!/usr/bin/env bash
# .devcontainer/post-create.sh
# Works in both WSL2 native and dev container contexts.
# Tool installation, venv and first build live in scripts/setup-workspace.sh;
# this script only adds the container-specific shell and Claude wiring.

set -e

# Resolve repo root regardless of context
REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
cd "$REPO"

echo "=== openobsw dev container setup ==="
echo "    Repo: $REPO"

# ── Tools, venv, build + test ─────────────────────────────────────────
bash "$REPO/scripts/setup-workspace.sh"

# ── Aliases (activate.sh also puts the cross toolchains on PATH) ──────
LINE="source $REPO/scripts/activate.sh"
grep -qxF "$LINE" ~/.bashrc || echo "$LINE" >> ~/.bashrc

# ── Git identity (persists in container) ─────────────────────────────
if [ -z "$(git config --global user.email)" ]; then
    echo "⚠  Git identity not set. Run:"
    echo "   git config --global user.email 'you@example.com'"
    echo "   git config --global user.name 'lipofefeyt'"
fi

# ── Global CLAUDE.md ──────────────────────────────────────────────────
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

echo ""
echo "=== openobsw ready ==="
echo "Run: source scripts/activate.sh"
