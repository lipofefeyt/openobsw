#!/usr/bin/env bash
# scripts/env.sh — sourced by activate.sh and setup-workspace.sh. Needs $REPO.
# Sets OPENOBSW_ENV (devcontainer | wsl | linux) and OPENOBSW_VENV.
#
# The same working tree is used from the devcontainer (/workspaces/openobsw)
# and from native WSL2 (~/workspace/openobsw). A venv hard-codes the path and
# the interpreter it was created with, so one made on either side is dead on
# the other. Each side therefore has its own: .venv in the devcontainer (and
# on plain Linux), .venv-wsl in native WSL2.

if [ -f /.dockerenv ] || [ -n "${REMOTE_CONTAINERS:-}" ] || [ -n "${CODESPACES:-}" ]; then
    OPENOBSW_ENV=devcontainer
elif grep -qi microsoft /proc/version 2>/dev/null; then
    OPENOBSW_ENV=wsl
else
    OPENOBSW_ENV=linux
fi

case "$OPENOBSW_ENV" in
    wsl) OPENOBSW_VENV="$REPO/.venv-wsl" ;;
    *)   OPENOBSW_VENV="$REPO/.venv" ;;
esac

export OPENOBSW_ENV OPENOBSW_VENV
