# 命令安全检查 Hook — 低风险自动放行，其余 AI 判断或弹窗
# PreToolUse hook for Bash|PowerShell
$ErrorActionPreference = "Stop"

# 使用 $input 自动变量读取管道数据（hook 框架通过 stdin 管道传入 JSON）
$rawInput = $input | Out-String
if (-not $rawInput -or $rawInput.Trim().Length -eq 0) {
    # 无输入，不做决策
    exit 0
}

try {
    $hookData = $rawInput | ConvertFrom-Json
} catch {
    return
}

$command = $hookData.tool_input.command
if (-not $command) {
    return
}

# ── 特征提取 ─────────────────────────────────────────
# 取第一条命令（跳过 && || ; 连接符和管道 |）
$firstCmd = ($command -split '[;&|]' | Select-Object -First 1).Trim()
if (-not $firstCmd) { return }

$tokens = $firstCmd -split '\s+'
$mainCmd = ($tokens[0] -replace '^.*[/\\]', '').ToLower()  # 剥离路径前缀
# ── 第一层：静态白名单 ───────────────────────────────
$lowRiskPatterns = @(
    # 读操作
    '^get-content$', '^get-childitem$', '^ls$', '^dir$', '^cat$',
    '^select-string$', '^test-path$', '^where(\.exe)?$', '^measure-object$',
    '^select-object$', '^format-', '^write-output$', '^echo$', '^type$',
    '^findstr$', '^tasklist$',
    # 构建/测试
    '^cmake$', '^ctest$', '^msbuild$',
    # 文件创建
    '^new-item$', '^out-file$', '^set-content$', '^mkdir$',
    # 环境变量
    '^set$', '^export$', '^printenv$',
    # Git 只读
    '^git$'
)

$isLowRisk = $false
$reason = ""

foreach ($pattern in $lowRiskPatterns) {
    if ($mainCmd -match $pattern) {
        $isLowRisk = $true
        $reason = "静态规则: $mainCmd"
        break
    }
}

# Git 命令细分：只读放行，其他不判定
if ($mainCmd -eq 'git' -and $tokens.Count -ge 2) {
    $gitSub = $tokens[1].ToLower()
    $gitReadOnly = @('status', 'log', 'diff', 'show', 'rev-parse', 'remote', 'stash', 'branch', 'tag', 'config', 'ls-files', 'ls-tree', 'describe', 'blame', 'grep', 'rev-list', 'whatchanged', 'shortlog', 'reflog', 'cherry', 'format-patch', 'archive', 'bundle', 'verify-commit', 'verify-tag', 'check-ref-format', 'check-ignore', 'check-attr', 'check-mailmap', 'column', 'credential', 'difftool', 'fetch', 'help', 'instaweb', 'mergetool', 'notes', 'range-diff', 'replace', 'request-pull', 'show-branch')
    if ($gitSub -in $gitReadOnly) {
        $isLowRisk = $true
        $reason = "静态规则: git $gitSub (只读)"
    } else {
        $isLowRisk = $false
    }
}

# 环境变量赋值模式
if ($firstCmd -match '^\$\w+\s*=' -or $firstCmd -match '^[A-Z_]+\s*=') {
    $isLowRisk = $true
    $reason = "静态规则: 环境变量赋值"
}

# 危险操作直接跳过静态规则（不进白名单）
$dangerousPatterns = @(
    '\brm\s+.*\b-rf?\b|\brmdir\b|\bremove-item\b.*\b-recurse\b',
    '\bcurl\b|\bwget\b|\binvoke-webrequest\b|\binvoke-restmethod\b',
    '\btaskkill\b|\bstop-process\b|\bkill\b|\bkillall\b',
    '\breg\s+add\b|\breg\s+delete\b|\bicacls\b|\bset-service\b',
    '\bgit\s+push\b.*-f|\bgit\s+reset\s+--hard\b|\bgit\s+clean\b',
    '\brunas\b|\bsudo\b|\bstart-process\b',
    '\bdel\s+/[sfq]\b|\brd\s+/[sq]\b',
    '\binvoke-expression\b|\biex\b',
    '\bset-executionpolicy\b|\bunblock-file\b'
)
foreach ($pattern in $dangerousPatterns) {
    if ($firstCmd -match $pattern) {
        $isLowRisk = $false
        break
    }
}

if ($isLowRisk) {
    Write-Output (ConvertTo-Json -Compress -Depth 3 @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
            permissionDecision = "allow"
            permissionDecisionReason = $reason
        }
    })
    exit 0
}

# ── 第二层：AI 语义判断 ──────────────────────────────
$apiKey = $env:ANTHROPIC_API_KEY
if (-not $apiKey) {
    # 无 API Key，不做决策
    Write-Output (ConvertTo-Json -Compress -Depth 2 @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
        }
    })
    exit 0
}

$prompt = @"
你是命令安全分类器。判断以下命令是否可以安全执行。
只输出一个 JSON: {"allow":true|false,"reason":"简短中文理由"}

命令: $command
"@

$body = @{
    model = "claude-haiku-4-5-20251001"
    max_tokens = 100
    messages = @(
        @{
            role = "user"
            content = $prompt
        }
    )
} | ConvertTo-Json -Depth 3

try {
    $response = Invoke-RestMethod `
        -Uri "https://api.anthropic.com/v1/messages" `
        -Method Post `
        -Headers @{
            "x-api-key" = $apiKey
            "anthropic-version" = "2023-06-01"
            "content-type" = "application/json"
        } `
        -Body $body `
        -TimeoutSec 3

    $aiText = $response.content[0].text.Trim()
    # 提取 JSON（AI 可能包裹在 markdown 代码块中）
    if ($aiText -match '\{[\s\S]*\}') {
        $aiJson = $Matches[0]
        $aiResult = $aiJson | ConvertFrom-Json
        if ($aiResult.allow -eq $true) {
            Write-Output (ConvertTo-Json -Compress -Depth 3 @{
                hookSpecificOutput = @{
                    hookEventName = "PreToolUse"
                    permissionDecision = "allow"
                    permissionDecisionReason = "AI判断: $($aiResult.reason)"
                }
            })
            exit 0
        }
    }
} catch {
    # 超时或请求失败，不做决策
}

# 不做决策，走正常弹窗
Write-Output (ConvertTo-Json -Compress -Depth 2 @{
    hookSpecificOutput = @{
        hookEventName = "PreToolUse"
    }
})
