#!/bin/bash
# setup-symlinks.sh
# 在新设备上建立 Claude Code 所需的 symlink，使项目记忆跟随 git 仓库跨设备同步
# 用法：chmod +x .claude/scripts/setup-symlinks.sh && ./.claude/scripts/setup-symlinks.sh

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CLAUDE_PROJECTS="$HOME/.claude/projects"

echo "========================================"
echo "  Symlink Setup"
echo "  Project: $PROJECT_DIR"
echo "========================================"

# ===========================================================================
# 1. Memory symlink: ~/.claude/projects/<slug>/memory → <project>/.claude/memory
# Slug derivation: replace / and : with -, e.g. /home/user/project → -home-user-project
# ===========================================================================
SLUG="${PROJECT_DIR//\//-}"
SLUG="${SLUG//:/-}"
FOUND_DIR=$(find "$CLAUDE_PROJECTS" -maxdepth 1 -type d -name "$SLUG" 2>/dev/null | head -1)

if [ -z "$FOUND_DIR" ]; then
    echo ""
    echo "[memory] [SKIP] Claude project directory not found under $CLAUDE_PROJECTS"
    echo "        Run Claude Code in this project at least once first."
else
    MEMORY_LINK="$FOUND_DIR/memory"
    TARGET="$PROJECT_DIR/.claude/memory"

    echo ""
    if [ -L "$MEMORY_LINK" ]; then
        echo "[memory] Symlink already exists: $MEMORY_LINK → $(readlink "$MEMORY_LINK")"
    elif [ -d "$MEMORY_LINK" ]; then
        echo "[memory] Removing existing directory..."
        rm -rf "$MEMORY_LINK"
        ln -s "$TARGET" "$MEMORY_LINK"
        echo "[memory] [OK] Symlink created: $MEMORY_LINK → $TARGET"
    else
        ln -s "$TARGET" "$MEMORY_LINK"
        echo "[memory] [OK] Symlink created: $MEMORY_LINK → $TARGET"
    fi
fi


echo ""
echo "========================================"
echo "  Setup complete."
echo "========================================"
