#!/bin/bash
# setup-memory-symlink.sh
# 在新设备上建立 Claude 项目记忆 symlink，使记忆跟随 git 仓库跨设备同步
# 用法：chmod +x scripts/setup-memory-symlink.sh && ./scripts/setup-memory-symlink.sh

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CLAUDE_PROJECTS="$HOME/.claude/projects"

# 查找包含 CrossRemoteDesktop 的项目目录
FOUND_DIR=$(find "$CLAUDE_PROJECTS" -maxdepth 1 -type d -name "*CrossRemoteDesktop*" 2>/dev/null | head -1)

if [ -z "$FOUND_DIR" ]; then
    echo "Claude project directory not found under $CLAUDE_PROJECTS"
    echo "Please run Claude Code in this project at least once first."
    exit 1
fi

MEMORY_LINK="$FOUND_DIR/memory"
TARGET="$PROJECT_DIR/.claude/memory"

if [ -L "$MEMORY_LINK" ]; then
    echo "Memory symlink already exists: $MEMORY_LINK → $(readlink "$MEMORY_LINK")"
    exit 0
fi

if [ -d "$MEMORY_LINK" ]; then
    echo "Removing existing memory directory..."
    rm -rf "$MEMORY_LINK"
fi

ln -s "$TARGET" "$MEMORY_LINK"
echo "[OK] Memory symlink created: $MEMORY_LINK → $TARGET"
