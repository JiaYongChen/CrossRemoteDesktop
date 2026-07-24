$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$cwd = (Get-Location).Path

$cwdNormalized = $cwd.Replace('\', '/').TrimEnd('/')
$rootNormalized = $root.Replace('\', '/').TrimEnd('/')

if ($cwdNormalized -ne $rootNormalized) {
    $msg = "[MANDATORY] CWD=$cwdNormalized != $rootNormalized. Run: cd $rootNormalized"
    $json = @{
        hookSpecificOutput = @{
            hookEventName = "PostToolUse"
            additionalContext = $msg
        }
    } | ConvertTo-Json -Compress
    Write-Output $json
}
