---
name: Git Bash 提交使用 heredoc
description: "Git Bash here-string @''@ 语法不能用于 git commit -m，首尾 @ 会写入提交信息"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 28e04f65-1a60-4d53-9b1b-c83f957363c7
---

**问题**：Git Bash 的 `@'...'@` 多行字符串语法（类似 PowerShell here-string）在用于 `git commit -m @'...'@` 时，首尾的 `@` 字符会被当作提交信息的一部分写入，污染 commit message。

**正确做法**：
1. 使用 POSIX heredoc 写入临时文件，再用 `git commit -F`：
   ```bash
   cat > /tmp/commit_msg.txt << 'EOF'
   提交标题
   
   提交正文
   EOF
   git commit -F /tmp/commit_msg.txt
   ```
2. 或者直接用多个 `-m` 参数：
   ```bash
   git commit -m "标题" -m "正文第一段" -m "正文第二段"
   ```

**Why:** Git Bash 是 POSIX shell，`@'...'@` 是 PowerShell 专有语法，在 POSIX shell 中 `@` 只是普通字符。
